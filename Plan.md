# 🎧 Real-Time Audio Streaming System

### Low Latency UDP-Based Design

---

## 🧩 Problem Focus

* Real-time audio transmission over network
* Pre-implementation delay estimation
* Runtime latency measurement
* Performance validation under network variability

---

## 🧠 Requirement Decomposition

**Core Objectives**

* Near real-time audio transfer
* Continuous playback
* Per-packet latency measurement

**Constraints**

* Low latency
* Network variability (jitter, packet loss)

**Latency Targets**

* Acceptable: < 50 ms
* Target: 10–30 ms
* Stretch (LAN): ~10 ms

---

## ⚙️ System Approach

**Architecture**
Mic → Frame → Packet → UDP → Receive → Buffer → Playback

**Protocol Choice**

* UDP → low latency, no retransmission
* Full control over packet timing

---

## 🧱 Tech Stack

* PortAudio → audio capture/playback
* BSD Sockets → UDP communication
* libsndfile → optional file saving
* Language → C++

---

## 📐 Frame Configuration

* Sample Rate: 48,000 Hz
* Frame Duration: 4 ms

**Calculation**

* Samples/frame = 48000 × 0.004 = 192
* Bytes/frame = 192 × 2 = 384 bytes

---

## 🎯 Frame Size Tradeoff

| Frame    | Latency | CPU    | Network |
| -------- | ------- | ------ | ------- |
| 20 ms    | High    | Low    | Low     |
| 10 ms    | Medium  | Medium | Medium  |
| **4 ms** | Low     | Higher | Higher  |

**Selected: 4 ms**

* Reduced base latency
* Acceptable system load

---

## 📦 Buffering Strategy

* Jitter buffer: 1–2 packets

**Buffer Delay**

* 2 × 4 ms = 8 ms

**Tradeoff**

* Small buffer → low latency
* Risk: minor audio glitches

---

## 🧮 Latency Model

**Total Delay**
= Frame + Network + Buffer

**Values**

* Frame: 4 ms
* Network: 2–5 ms
* Buffer: 8 ms 

**Expected Latency**
≈ 14–17 ms

---

## 🔄 Adaptive Strategy

**Dynamic Adjustment**

* High jitter → increase buffer
* High delay → increase frame size
* Stable network → reduce frame size

**Example**

* Normal: 4 ms
* High jitter: 6–8 ms

**Goal**

* Maintain stability + low latency

---

## 📡 Packet Structure

* Sequence Number → loss detection
* Timestamp → latency measurement
* Audio Data

---

## 📏 Performance Measurement

**Latency Calculation**
delay = receive_time − send_timestamp

**Metrics**

* Per-packet delay
* Average delay
* Maximum delay
* Jitter

---

## 📊 Observed Output (Example)

* Packet 101 → 15 ms
* Packet 102 → 16 ms
* Packet 103 → 14 ms

**Average**
≈ 15 ms

---

## ⚖️ Trade-offs

**Advantages**

* Low latency
* No retransmission overhead
* Fine-grained control
* Adaptive behavior

**Disadvantages**

* No packet recovery (UDP)
* Possible glitches
* Higher CPU usage
* Added system complexity

---

## 🧪 Validation Strategy

* Expected vs actual latency comparison
* Network variation testing
* Packet loss observation
* Adaptive tuning evaluation

---

## 🏁 Outcome

* Achieved near real-time audio transmission
* Predicted latency ≈ 15 ms
* Measurable and verifiable performance
* Stable under varying conditions

---

## 🔥 Key Insight

Low latency is achieved by optimizing:

* Frame size
* Buffer size
* Transport protocol

While balancing:

* Stability
* Reliability
* System load

---
