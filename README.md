# Real-Time UDP Audio Streaming — Low Latency Audio Pipeline

A real-time, low-latency audio streaming system built in **C++ using PortAudio and Winsock UDP**, designed to achieve **10–30ms mouth-to-ear latency** over a LAN or localhost.

## Project Overview

This project captures live microphone audio on a **Sender** machine, transmits it in small UDP packets, and plays it back in near real-time on a **Receiver** machine. A custom jitter buffer absorbs network irregularities while maintaining bounded latency.

---

## Architecture

```
[Microphone]
     │
     ▼
[Pa_ReadStream]   ← PortAudio captures 4ms of audio (192 frames @ 48kHz)
     │
     ▼
[AudioPacket]     ← struct { sequence, timestamp_µs, audio[192] }
     │
     ▼
[sendto() UDP]    ← Winsock sends ~392-byte UDP datagram to port 9000
     │  network
     ▼
[recvfrom() UDP]  ← Non-blocking select() drains OS socket buffer
     │
     ▼
[Jitter Buffer]   ← Software queue of 4 packets (~16ms headroom)
     │
     ▼
[Pa_WriteStream]  ← PortAudio plays audio through speakers
     │
     ▼
[Speakers]
```

---

## Packet Structure

```cpp
struct AudioPacket {
    uint32_t sequence;            // Packet counter for loss detection
    uint64_t timestamp;           // Capture time in microseconds (steady_clock)
    int16_t  audio[FRAMES_PER_BUFFER]; // Raw 16-bit mono PCM audio
};
// Total size: 4 + 8 + (192 × 2) = 396 bytes per packet
```

---

## Dependencies

| Dependency | Purpose |
|---|---|
| **PortAudio** | Cross-platform audio I/O |
| **Winsock2** (`ws2_32`) | UDP socket networking on Windows |
| **MSYS2 (ucrt64)** | MinGW C++ build environment |
| **Python 3** | Test automation (`run_test_suite.py`) |
| **pandas + matplotlib** | CSV analysis & graph generation |

### Install PortAudio (MSYS2)

```bash
pacman -S mingw-w64-ucrt-x86_64-portaudio
```

### Install Python dependencies

```bash
pip install pandas matplotlib
```

---

## Compilation

Open **MSYS2 ucrt64** terminal in the project directory:

```bash
# Compile sender
g++ sender_audio.cpp -o sender_audio -lws2_32 -lportaudio

# Compile receiver
g++ receiver_audio.cpp -o receiver_audio -lws2_32 -lportaudio
```

---

## Running the System

### Step 1 — Start the Receiver first

```bash
./receiver_audio
```

Output:
```
Receiving audio... (Press Ctrl+C to stop)

========================================
Component          Delay
========================================
Frame size         4 ms
Audio buffer       ~8 ms
Network            0.12 ms
Jitter buffer      ~4 ms
----------------------------------------
TOTAL LATENCY      ~16.12 ms
========================================
```

### Step 2 — Start the Sender

```bash
./sender_audio 
```

Output:
```
Sending audio... (Press Ctrl+C to stop)
```

> **Note:** The sender prints `O` characters when a PortAudio input overflow occurs. These are harmless — the system continues streaming.

### Step 3 — Stop gracefully

Press `Ctrl+C` in either terminal window. Both programs clean up gracefully:
- Closes the UDP socket
- Stops and closes the PortAudio stream
- Terminates Winsock

---

## Audio Configuration

All configuration is done via `#define` constants at the top of each file:

| Constant | Default | Description |
|---|---|---|
| `SAMPLE_RATE` | `48000` | Hz — standard high-quality audio |
| `FRAMES_PER_BUFFER` | `192` | Frames per packet = 4ms at 48kHz |

### Frame Size vs Latency Tradeoff

| Frame Size | FRAMES_PER_BUFFER | Latency | Jitter Risk | CPU |
|---|---|---|---|---|
| 8 ms | 384 | ~17 ms | Low | Low |
| **4 ms** | **192** | **~13 ms** | **Medium** | **Medium** |
| 2 ms | 96 | ~11 ms | Higher | Higher |

To change frame size, modify `FRAMES_PER_BUFFER` in **both** files and recompile.

---

## Logging Latency to CSV

The receiver accepts an optional CSV path argument:

```bash
./receiver_audio results/4ms_test/latency_log.csv
```

The CSV captures per-packet metrics:

```
Sequence,ExpectedTotal_ms,Network_ms,JitterSize_pkts,Jitter_ms,Total_ms
1,13.0,0.12,0,0,12.12
2,13.0,0.09,1,4,16.09
...
```

---

## Automated Test Suite

`run_test_suite.py` automatically runs tests across all configured frame sizes and generates Matplotlib graphs.

```bash
python run_test_suite.py
```

What it does:
1. Modifies `FRAMES_PER_BUFFER` in both `.cpp` files
2. Recompiles both executables
3. Runs sender + receiver simultaneously for **10 seconds** each
4. Saves CSV logs and renders two PNG graphs per test

Output folder structure:
```
results/
├── 8ms_test/
│   ├── latency_log.csv
│   ├── expected_vs_actual.png
│   └── components_breakdown.png
├── 4ms_test/
│   └── ...
└── 2ms_test/
    └── ...
```

After the suite completes, both `.cpp` files are restored to **4ms (192 frames)**.

---

## Latency Measurement

Latency is calculated on the **receiver** using the packet's embedded timestamp:

```cpp
double network_ms = (getTimeMicroseconds() - packet.timestamp) / 1000.0;
```

> **Important:** This is only valid when sender and receiver run on the **same machine** (localhost). On two separate physical machines, clock epoch differences will produce incorrect values. A proper two-machine setup requires RTT-based clock synchronization (e.g., NTP or custom ping-pong).

### Latency Component Breakdown

| Component | Source | Typical Value |
|---|---|---|
| Frame size | `FRAMES_PER_BUFFER / SAMPLE_RATE` | 4 ms |
| Audio buffer (DAC) | Windows hardware queue | ~8 ms |
| Network transit | `recvfrom` timestamp delta | ~0.05 – 7 ms |
| Jitter buffer | Software queue × frame size | ~0 – 12 ms |
| **Total** | | **~12 – 28 ms** |

---

## Key Engineering Decisions

### Non-blocking Socket Drain (select + FIONBIO)
```cpp
u_long mode = 1;
ioctlsocket(sock, FIONBIO, &mode); // Set non-blocking
// Then drain all packets in a tight inner while loop
```
Prevents OS-level buffer bloat from accumulating queued packets from missed loop iterations.

### Spin-lock Pacing (90% frame duration)
```cpp
uint64_t target_time = last_send_time + (frame_duration_us * 9 / 10);
while (getTimeMicroseconds() < target_time) {
    std::this_thread::yield();
}
```
Prevents burst sending while allowing the sender to catch up 10% faster than real-time, clearing hardware input backlogs without risking the Windows 15.6ms sleep penalty.

### Overflow-Safe Packet Handling
```cpp
PaError err = Pa_ReadStream(stream, buffer, FRAMES_PER_BUFFER);
if (err == paInputOverflowed) {
    // Continue — don't skip the packet, just note it
}
```
Returning `continue` on overflow was previously causing a "death spiral" — the sender would skip sending and fall further behind indefinitely.

### Socket Send Buffer Tuning
```cpp
int bufSize = 65536;
setsockopt(sock, SOL_SOCKET, SO_SNDBUF, (char*)&bufSize, sizeof(bufSize));
```
Prevents packet drops under heavy OS network load.

---

## Known Limitations

| Issue | Notes |
|---|---|
| `O` printed during sender startup | PortAudio input overflow during initialization. Harmless — clears within 1–2 seconds |
| Clock mismatch on two machines | `steady_clock` is per-machine — use NTP or RTT sync for cross-machine measurement |
| Jitter buffer invisible to software | Windows DAC pre-buffers packets silently; `jitterBuffer.size()` may read 0 even when packets are buffered in hardware |
| Windows timer resolution | `sleep_for(1ms)` resolves to ~15.6ms — use spin-lock yields instead |

---

## Target Latency Goals

| Category | Target |
|---|---|
| Acceptable | < 50 ms |
| Target | 10 – 30 ms |
| Stretch (LAN) | ~10 ms |

**Achieved on localhost:** consistent **12 – 28 ms** total latency at 4ms frame size.

---

## File Reference

| File | Purpose |
|---|---|
| `sender_audio.cpp` | Microphone capture + UDP sender |
| `receiver_audio.cpp` | UDP receiver + jitter buffer + audio playback |
| `run_test_suite.py` | Automated multi-config test + graph generator |
| `results.md` | Test results summary |
| `results/` | CSV logs and PNG graphs per test config |
