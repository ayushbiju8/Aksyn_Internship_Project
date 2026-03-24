// receiver_audio.cpp
#include <iostream>
#include <cstdint>
#include <cstring>
#include <chrono>
#include <queue>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <portaudio.h>
#include <atomic>
#include <csignal>
#include <thread> 
#include <fstream>

// Link the Winsock library for network functionality
#pragma comment(lib, "ws2_32.lib")

// --- Audio Configuration ---
// Must match the sender exactly: 48kHz and 192 frames (4ms of audio)
#define SAMPLE_RATE 48000
#define FRAMES_PER_BUFFER 192

// 📦 Packet structure
// Must exactly match the sender's structure to deserialize UDP payload properly
struct AudioPacket {
    uint32_t sequence;        // To detect packet loss
    uint64_t timestamp;       // Sender's timestamp for latency tracking
    int16_t audio[FRAMES_PER_BUFFER]; // The raw audio PCM data
};

// ⏱️ Get current time in microseconds
// Used for latency calculations when a packet arrives
uint64_t getTimeMicroseconds() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()
    ).count();
}

// Global flag allowing main loop to run until Ctrl+C is pressed
std::atomic<bool> keep_running{true};

// Signal handler to gracefully handle program termination via Ctrl+C
void signalHandler(int signum) {
    std::cout << "\nInterrupt signal (" << signum << ") received. Stopping gracefully...\n";
    keep_running = false;
}

int main(int argc, char* argv[]) {
    // ==========================================
    // 1. Analytics & Log Initialization
    // ==========================================
    
    // Default CSV file for logging system latency metrics
    std::string csv_file = "latency_log.csv";
    if (argc > 1) {
        csv_file = argv[1]; // Optionally override file path via command line args
    }
    
    // Open the CSV file and write the header row
    std::ofstream logFile(csv_file);
    if (logFile.is_open()) {
        logFile << "Sequence,ExpectedTotal_ms,Network_ms,JitterSize_pkts,Jitter_ms,Total_ms\n";
    }

    // ==========================================
    // 2. Network Initialization (Winsock & UDP Binding)
    // ==========================================
    
    WSADATA wsa;
    WSAStartup(MAKEWORD(2,2), &wsa);

    // Create our UDP listening socket
    int sock = socket(AF_INET, SOCK_DGRAM, 0);

    // Bind socket to port 9000 to listen for incoming sender packets
    sockaddr_in server{};
    server.sin_family = AF_INET;
    server.sin_port = htons(9000);
    server.sin_addr.s_addr = INADDR_ANY; // Accept packets from any IP address

    bind(sock, (sockaddr*)&server, sizeof(server));

    // ==========================================
    // 3. Audio Initialization (PortAudio)
    // ==========================================

    Pa_Initialize();

    // Setup output audio parameters for the speaker/headphones
    PaStreamParameters outputParameters;
    outputParameters.device = Pa_GetDefaultOutputDevice(); // Use system default speaker
    if (outputParameters.device == paNoDevice) {
        std::cout << "Error: No default output device.\n";
        return -1;
    }
    outputParameters.channelCount = 1; // Mono playback
    outputParameters.sampleFormat = paInt16; // 16-bit PCM format
    // Request driver to provide the absolute minimum possible output latency buffering
    outputParameters.suggestedLatency = Pa_GetDeviceInfo(outputParameters.device)->defaultLowOutputLatency;
    outputParameters.hostApiSpecificStreamInfo = NULL;

    PaStream* stream;
    // Open PortAudio stream for output only
    if (Pa_OpenStream(&stream, NULL, &outputParameters,
        SAMPLE_RATE, FRAMES_PER_BUFFER, paClipOff, NULL, NULL) != paNoError) {
        std::cout << "PortAudio error\n";
        return -1;
    }

    Pa_StartStream(stream);

    // ==========================================
    // 4. Socket Tuning & Jitter Buffer State
    // ==========================================

    // 🚀 Make socket non-blocking
    // This is CRITICAL. It allows the receiver to continuously drain the network buffer,
    // push packets directly to the jitter buffer, and avoid waiting indefinitely for data.
    u_long mode = 1;
    ioctlsocket(sock, FIONBIO, &mode);

    // The Jitter Buffer is used to absorb variations in network packet arrival time (network jitter).
    std::queue<AudioPacket> jitterBuffer;
    
    // Provide a small buffer (4 packets = 16ms of audio) before starting playback.
    // This smooths over network hiccups without creating massive delays.
    const size_t BUFFER_SIZE = 4; 
    
    uint32_t lastSequence = 0; // Tracks the last received packet sequence to detect loss
    bool playing = false;      // Master state indicating if we have enough buffer to play audio

    signal(SIGINT, signalHandler);
    std::cout << "Receiving audio... (Press Ctrl+C to stop)\n";

    // ==========================================
    // 5. Main Receive & Playback Loop
    // ==========================================

    while (keep_running) {
        // ⏱️ Step A: Network Polling / Block State Decision
        // We use the `select()` system call to wait for network activity.
        // If we are already playing audio and have packets buffered, we don't need to block:
        // we can poll instantly and proceed to play audio. 
        // If the buffer is empty, we must block slightly until a packet arrives to prevent CPU spinning.
        bool should_block = true;
        if (playing && !jitterBuffer.empty()) {
            should_block = false;
        }

        // Define blocking logic (wait up to 100ms for data when starved)
        timeval tv_block;
        tv_block.tv_sec = 0;
        tv_block.tv_usec = 100000; 
        
        // Define non-blocking logic (just check and move on)
        timeval tv_noblock;
        tv_noblock.tv_sec = 0;
        tv_noblock.tv_usec = 0;

        // Register our socket to be watched by `select`
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(sock, &readfds);

        // Wait for incoming data based on our blocking state choice
        select(0, &readfds, NULL, NULL, should_block ? &tv_block : &tv_noblock);

        // 🔄 Step B: Drain all available packets in the OS socket buffer
        // Instead of reading ONE packet, we read ALL currently queued packets.
        // This stops "buffer bloat" inside the Windows networking stack itself.
        while (true) {
            AudioPacket packet;
            // Attempt to read since socket is non-blocking
            int len = recvfrom(sock, (char*)&packet, sizeof(packet), 0, NULL, NULL);
            if (len > 0) {
                // Packet arrived! Let's check for missed gaps in sequence.
                if (lastSequence != 0 && packet.sequence != lastSequence + 1) {
                    std::cout << "Packet loss! Expected: " << (lastSequence + 1) << " Got: " << packet.sequence << "\n";
                }
                lastSequence = packet.sequence;

                // ⏱️ Calculate metrics for the newly arrived packet
                // Latency is the time it took between the sender assembling the packet and now.
                double latency = (getTimeMicroseconds() - packet.timestamp) / 1000.0;
                
                double frame_size_ms = (FRAMES_PER_BUFFER * 1000.0) / SAMPLE_RATE; // Always 4ms
                double jitter_ms = jitterBuffer.size() * frame_size_ms;            // Delay from packets sitting in our queue
                double audio_buffer_ms = 8.0;                                      // Approximate driver/hardware level delay
                double network_ms = latency;                                       // Time spent travelling through network
                
                // Real-world expected latency based on actual measurements
                double total_latency = frame_size_ms + audio_buffer_ms + network_ms + jitter_ms;
                
                // Theoretical expected baseline limit mathematically possible for our system
                // (PortAudio absorbs explicit Jitter Buffer chunks silently directly to hardware)
                double expected_latency = frame_size_ms + audio_buffer_ms + 1.0;

                // Log metrics for graphing and monitoring
                if (logFile.is_open()) {
                    logFile << packet.sequence << "," 
                            << expected_latency << ","
                            << network_ms << ","
                            << jitterBuffer.size() << ","
                            << jitter_ms << ","
                            << total_latency << "\n";
                }
                
                // 📊 Step C: Console Output Updates
                // Display granular breakdown every 50 packets to not overload the I/O thread
                if (packet.sequence % 50 == 0) {
                    std::cout << "\n========================================\n";
                    std::cout << "Component\t\tDelay\n";
                    std::cout << "========================================\n";
                    std::cout << "Frame size\t\t" << frame_size_ms << " ms\n";
                    std::cout << "Audio buffer\t\t~" << audio_buffer_ms << " ms\n";
                    std::cout << "Network\t\t\t" << network_ms << " ms\n";
                    std::cout << "Jitter buffer\t\t~" << jitter_ms << " ms\n";
                    std::cout << "----------------------------------------\n";
                    std::cout << "TOTAL LATENCY\t\t~" << total_latency << " ms\n";
                    std::cout << "========================================\n";
                }

                // Append the packet to the jitter buffer
                jitterBuffer.push(packet);
            } else {
                // `recvfrom` returned <= 0, which means WSAEWOULDBLOCK triggered.
                // There are no more packets waiting in the OS queue right now. Break the drain loop.
                break; 
            }
        }

        // 🗑️ Step D: Active Jitter Buffer Management
        // If our queue grows too large (Buffer bloat), it introduces massive latency.
        // We aggressively trim the buffer to keep it clamped at BUFFER_SIZE + 2.
        while (jitterBuffer.size() > BUFFER_SIZE + 2) {
            jitterBuffer.pop();
        }

        // 🎛️ Step E: State Machine update
        // Wait until we have a healthy cushion (BUFFER_SIZE) to start playback.
        if (jitterBuffer.size() >= BUFFER_SIZE) {
            playing = true;
        } else if (jitterBuffer.empty()) {
            // Buffer dried up! We must pause playback and let it refill to avoid clicking and stuttering.
            playing = false;
        }

        // ▶️ Step F: Audio Playback
        // Send exactly 1 packet frame to the soundcard per loop execution
        if (playing) {
            AudioPacket playPacket = jitterBuffer.front();
            jitterBuffer.pop(); // Remove packet from queue

            // Write blocking call: hands off the 192 audio samples to PortAudio speaker drivers
            PaError err = Pa_WriteStream(stream, playPacket.audio, FRAMES_PER_BUFFER);
            if (err != paNoError && err != paOutputUnderflowed) {
                // Alert if passing to portaudio failed
                std::cout << "PortAudio write error: " << Pa_GetErrorText(err) << "\n";
            }
        }
    }

    // ==========================================
    // 6. Graceful Cleanup
    // ==========================================
    
    // Stop audio, terminate portaudio, and free network handles
    Pa_StopStream(stream);
    Pa_CloseStream(stream);
    Pa_Terminate();
    
    closesocket(sock);
    WSACleanup();
    
    std::cout << "Receiver stopped cleanly.\n";
    return 0; // Exit successfully
}