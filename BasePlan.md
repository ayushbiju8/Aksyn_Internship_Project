# 🎧 Real-Time Audio Streaming (Low Latency) – Implementation Guide

This document is a **hands-on implementation reference** for building a real-time audio transmission system with latency measurement.

No theory. Only what you need to build.

---

# 🚀 Core Approach

We are building a **packet-based audio streaming system over UDP**.

Pipeline:

Mic → Audio Frames → Packetize → UDP Send → UDP Receive → Buffer → Playback

---

# 🧱 Tech Stack

## Audio I/O
- PortAudio OR RtAudio

## Networking
- BSD Sockets (UDP)

## File Saving (Optional)
- libsndfile

## Language
- C++

---

# ⚙️ System Architecture

## Sender (Node A)
1. Capture audio from mic
2. Split into small frames (VERY IMPORTANT)
3. Wrap into packet
4. Send via UDP immediately

## Receiver (Node B)
1. Receive UDP packets
2. Store in small buffer (jitter buffer)
3. Play audio instantly
4. Save to file (optional)
5. Measure delay

---

# 🎯 Latency Target Strategy

## Goal
- Ideal: < 20 ms
- Stretch: ~10 ms (only on LAN)

---

# 🔥 CRITICAL LATENCY DECISIONS

## 1. Frame Size (MOST IMPORTANT)

Use:
- 5 ms frame (recommended)
- 2–3 ms (aggressive)

Why:
- Frame size directly = base latency

Example:
- 20 ms frame → already 20 ms delay ❌
- 5 ms frame → only 5 ms delay ✅

---

## 2. Audio Configuration

Use:
- Sample rate: 48000 Hz
- Channels: Mono (1)

Why:
- Lower data = faster transmission

---

## 3. NO Encoding

Use:
- Raw PCM only

Avoid:
- MP3 / Opus / compression

Why:
- Encoding adds delay

---

## 4. UDP Only

Use:
- UDP sockets

Avoid:
- TCP

Why:
- TCP retransmission = latency spike

---

## 5. Minimal Buffering

Sender:
- Send immediately after capture

Receiver:
- Buffer only 1–2 packets

Tradeoff:
- Lower latency ⚡
- Slight audio glitches ⚠️

---

## 6. Small Audio Buffers

PortAudio config:
- framesPerBuffer = 64 or 128

Why:
- Smaller buffer = lower delay

---

# 📦 Packet Design

```cpp
struct AudioPacket {
    uint32_t sequence_number;
    uint64_t timestamp;   // send time (microseconds)
    char audio_data[FRAME_SIZE];
};