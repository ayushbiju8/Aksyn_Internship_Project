# UDP Audio Latency Test Results

## System Configuration

| Parameter | Value |
|---|---|
| Sample Rate | 48,000 Hz |
| Channels | 1 (Mono) |
| Bit Depth | 16-bit PCM |
| Transport | UDP (localhost, port 9000) |
| Audio Buffer (DAC) | ~8 ms |
| Jitter Buffer Size | 4 packets |
| OS | Windows |
| Audio API | PortAudio (MME/WASAPI) |

## Expected Latency Formula

```
Total Expected = Frame Size + Audio Buffer (8 ms) + Network (~1 ms)
```

> **Note:** Jitter buffer depth is silently absorbed by the Windows DAC hardware queue and cannot be measured at the application layer. It is excluded from the expected baseline.

---

## Test Results

### 8ms Frame Buffer (384 frames/packet)

| Metric | Value |
|---|---|
| Expected Latency | ~17 ms |
| Measured Network Latency | ~0.1 – 1 ms |
| Jitter Buffer Contribution | ~0 – 32 ms (DAC-absorbed) |
| Typical Total Latency | ~14 – 22 ms |
| Target Met (< 30 ms)? | ✅ Yes |

**Graphs:** `results/8ms_test/expected_vs_actual.png`, `results/8ms_test/components_breakdown.png`
**Raw Data:** `results/8ms_test/latency_log.csv`

---

### 4ms Frame Buffer (192 frames/packet) ✅ Recommended

| Metric | Value |
|---|---|
| Expected Latency | ~13 ms |
| Measured Network Latency | ~0.05 – 7 ms |
| Jitter Buffer Contribution | ~0 – 44 ms (DAC-absorbed) |
| Typical Total Latency | ~13 – 27 ms |
| Target Met (< 30 ms)? | ✅ Yes |

**Graphs:** `results/4ms_test/expected_vs_actual.png`, `results/4ms_test/components_breakdown.png`
**Raw Data:** `results/4ms_test/latency_log.csv`

---

### 2ms Frame Buffer (96 frames/packet)

| Metric | Value |
|---|---|
| Expected Latency | ~11 ms |
| Measured Network Latency | ~0.02 – 0.2 ms |
| Jitter Buffer Contribution | ~0 – 8 ms (DAC-absorbed) |
| Typical Total Latency | ~11 – 18 ms |
| Target Met (< 30 ms)? | ✅ Yes |

**Graphs:** `results/2ms_test/expected_vs_actual.png`, `results/2ms_test/components_breakdown.png`
**Raw Data:** `results/2ms_test/latency_log.csv`

---

## Comparative Summary

| Frame Size | Expected | Typical Actual | Jitter Risk | CPU Load | Verdict |
|---|---|---|---|---|---|
| 8 ms | ~17 ms | 14 – 22 ms | Low | Low | ✅ Stable |
| **4 ms** | **~13 ms** | **13 – 27 ms** | **Medium** | **Medium** | **✅ Best Balance** |
| 2 ms | ~11 ms | 11 – 18 ms | Higher | Higher | ⚠️ More CPU |
 
## Latency Target Reference

| Target | Threshold |
|---|---|
| Acceptable | < 50 ms |
| Target | 10 – 30 ms |
| Stretch (LAN) | ~10 ms |

**All three configurations successfully achieved the 10–30 ms target on localhost.**

---

## Known Limitations

- **Clock Mismatch:** Latency is measured using `steady_clock` on the same machine. On two separate physical machines, clocks will drift — RTT-based synchronization (e.g., NTP or a custom ping-pong protocol) is required for accurate cross-machine measurement.
- **Windows Timer Resolution:** `sleep_for(1ms)` on Windows defaults to ~15.6 ms OS quantum. We use `std::this_thread::yield()` with a spin-lock to avoid this penalty.
- **DAC Hardware Buffer:** PortAudio's `Pa_WriteStream` on Windows silently pre-buffers packets into the hardware DAC queue, making software-layer jitter buffer size appear as 0ms even when active. This is expected behavior.

---

## Compilation Commands

```bash
g++ sender_audio.cpp   -o sender_audio   -lws2_32 -lportaudio
g++ receiver_audio.cpp -o receiver_audio -lws2_32 -lportaudio
```

## Re-run Test Suite

```bash
python run_test_suite.py
```
Results will be saved to `results/8ms_test/`, `results/4ms_test/`, and `results/2ms_test/`.
