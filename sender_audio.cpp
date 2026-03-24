// sender_audio.cpp
#include <iostream>
#include <cstdint>
#include <cstring>
#include <chrono>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <portaudio.h>
#include <atomic>
#include <csignal>
#include <thread>

// Link the Winsock library so network functions can be resolved during compilation
#pragma comment(lib, "ws2_32.lib")

// --- Audio Configuration ---
// 48 kHz is the standard sample rate for high-quality audio
#define SAMPLE_RATE 48000
// We use a small buffer (192 frames) to achieve very low latency.
// At 48kHz, 192 frames is exactly 4 milliseconds of audio per packet.
#define FRAMES_PER_BUFFER 192

// 📦 Packet structure
// This is the payload structure sent over the network.
struct AudioPacket {
    // Sequence number to track packet loss and out-of-order packets on the receiver side
    uint32_t sequence;
    // Timestamp (in microseconds) when the packet was captured, used to measure network latency
    uint64_t timestamp;
    // The actual raw 16-bit PCM audio data. Array size matches our frames per buffer.
    int16_t audio[FRAMES_PER_BUFFER];
};

// ⏱️ Get current time in microseconds
// Used for accurately timestamping packets to measure end-to-end latency
uint64_t getTimeMicroseconds() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()
    ).count();
}

// Global flag to control the main loop, allowing for a graceful shutdown on Ctrl+C
std::atomic<bool> keep_running{true};

// Signal handler to catch Ctrl+C (SIGINT)
void signalHandler(int signum) {
    std::cout << "\nInterrupt signal (" << signum << ") received. Stopping gracefully...\n";
    keep_running = false;
}

int main() {
    // ==========================================
    // 1. Network Initialization (Winsock & UDP)
    // ==========================================
    
    // 🔌 Initialize Winsock API (v2.2) required for networking on Windows
    WSADATA wsa;
    WSAStartup(MAKEWORD(2,2), &wsa);

    // Create a UDP socket. AF_INET = IPv4, SOCK_DGRAM = UDP.
    int sock = socket(AF_INET, SOCK_DGRAM, 0);

    // 🎛️ Socket buffer tuning
    // We increase the send buffer size to 64KB (65536 bytes) to prevent dropping packets
    // locally if the OS network stack momentarily falls behind our high packet rate.
    int bufSize = 65536;
    setsockopt(sock, SOL_SOCKET, SO_SNDBUF, (char*)&bufSize, sizeof(bufSize));

    // Setup the receiver's address structure.
    sockaddr_in receiver{};
    receiver.sin_family = AF_INET;
    receiver.sin_port = htons(9000); // Destination port 9000
    receiver.sin_addr.s_addr = inet_addr("127.0.0.1"); // Sending to localhost for testing

    // ==========================================
    // 2. Audio Initialization (PortAudio)
    // ==========================================

    // 🎤 Initialize the PortAudio library
    Pa_Initialize();

    // Configure the input audio stream parameters
    PaStreamParameters inputParameters;
    inputParameters.device = Pa_GetDefaultInputDevice(); // Use the system's default microphone
    if (inputParameters.device == paNoDevice) {
        std::cout << "Error: No default input device.\n";
        return -1;
    }
    inputParameters.channelCount = 1; // Mono audio
    inputParameters.sampleFormat = paInt16; // 16-bit PCM audio format
    // Request the absolute lowest possible latency from the audio driver
    inputParameters.suggestedLatency = Pa_GetDeviceInfo(inputParameters.device)->defaultLowInputLatency;
    inputParameters.hostApiSpecificStreamInfo = NULL;

    PaStream* stream;
    // Open the PortAudio stream with the specified parameters
    if (Pa_OpenStream(&stream, &inputParameters, NULL,
        SAMPLE_RATE, FRAMES_PER_BUFFER, paClipOff, NULL, NULL) != paNoError) {
        std::cout << "PortAudio error\n";
        return -1;
    }

    // Start actively capturing audio from the microphone
    Pa_StartStream(stream);

    // Local buffer to temporarily hold the captured audio frames
    int16_t buffer[FRAMES_PER_BUFFER];
    // Create and initialize our packet structure
    AudioPacket packet{};
    packet.sequence = 0; // Start sequence number at 0

    // Register our signal handler for Ctrl+C
    signal(SIGINT, signalHandler);
    std::cout << "Sending audio... (Press Ctrl+C to stop)\n";

    // Variables for our packet pacing algorithm
    uint64_t last_send_time = 0;
    // Calculate the expected duration of one frame buffer in microseconds
    // For 192 frames at 48000Hz, this is exactly 4000 microseconds (4ms).
    const uint64_t frame_duration_us = (FRAMES_PER_BUFFER * 1000000ULL) / SAMPLE_RATE;

    // ==========================================
    // 3. Main Capture & Transmission Loop
    // ==========================================
    while (keep_running) {
        // 🎤 Step A: Capture audio from the microphone
        // This function will block until exactly FRAMES_PER_BUFFER samples are ready.
        PaError err = Pa_ReadStream(stream, buffer, FRAMES_PER_BUFFER);
        
        // Handle read errors or warnings
        if (err == paInputOverflowed) {
            // Buffer overflow usually means we read too slowly and PortAudio had to drop audio hardware frames.
            std::cout << "O" << std::flush; // Print a tiny 'O' so it doesn't heavily block the critical audio thread
        } else if (err != paNoError) {
            std::cout << "\nPortAudio read error: " << Pa_GetErrorText(err) << "\n";
        }
        
        // 📦 Step B: Prepare the network packet
        packet.sequence++; // Increment packet sequence number
        packet.timestamp = getTimeMicroseconds(); // Record the exact time this audio was captured
        // Copy the raw audio data into the network packet payload
        memcpy(packet.audio, buffer, sizeof(buffer));

        // 📡 Step C: Send the packet over UDP
        int bytesSent = sendto(sock, (char*)&packet, sizeof(packet), 0,
                               (sockaddr*)&receiver, sizeof(receiver));
        
        if (bytesSent == SOCKET_ERROR) {
            std::cout << "sendto failed: " << WSAGetLastError() << "\n";
            continue;
        } else if (bytesSent != sizeof(packet)) {
            std::cout << "Partial send!\n"; // Check that the whole packet went out
        }

        // ⏱️ Step D: Packet Pacing / Burst Prevention
        // Sending too fast can wipe out the receiver's buffer or cause network queue build-ups.
        // We calculate a target time that spaces out our packet transmissions.
        // By using 90% of the frame duration, we effectively prevent clustered bursts 
        // while allowing the sender to catch up (~10% faster) if PortAudio has a localized backlog of data.
        uint64_t target_time = last_send_time + (frame_duration_us * 9 / 10);
        
        // Busy-wait (yield thread) until it is time to send the next packet
        while (getTimeMicroseconds() < target_time) {
            std::this_thread::yield();
        }
        
        // Update the last send time to the current time, ready for the next iteration
        last_send_time = getTimeMicroseconds();
    }

    // ==========================================
    // 4. Graceful Cleanup
    // ==========================================
    
    // Stop the stream and release the audio device
    Pa_StopStream(stream);
    Pa_CloseStream(stream);
    Pa_Terminate();
    
    // Close the network socket and cleanup Winsock
    closesocket(sock); 
    WSACleanup();
    
    std::cout << "Sender stopped cleanly.\n";
    return 0;
}