# BandwidthEstimator

C++17 library for estimating the available bandwidth. No external dependencies.

## Build

```
cmake -S . -B build
cmake --build build
ctest --test-dir build
```

| Option | Default | Effect |
|---|---|---|
| `BWE_BUILD_TESTS` | `ON` as top-level project, else `OFF` | `OFF`: tests are not built, GoogleTest is not needed |
| `BWE_BUILD_TOOLS` | `ON` as top-level project, else `OFF` | Test data generator |
| `BUILD_SHARED_LIBS` | `OFF` | Shared instead of static library |

Only the library: `cmake -S . -B build -DBWE_BUILD_TESTS=OFF -DBWE_BUILD_TOOLS=OFF`

## Usage

```cpp
auto estimator = bwe::EstimatorFactory::Instance().CreateSender(
    "hybrid", {{"aimd.headroom", 2.0}});
estimator->Update(measurement);  // periodically, e.g. every 100 ms to 1 s
bwe::BandwidthEstimate estimate = estimator->GetEstimate();
```

## Algorithms

| Name | Side | Signal |
|---|---|---|
| `mathis` | both | RTT, loss |
| `tfrc` | both | RTT, loss |
| `aimd` | both | loss, throughput |
| `delay` | both | RTT, throughput |
| `link_capacity` | both | link capacity, loss |
| `send_buffer` | sender | send buffer, throughput |
| `tsbpd_reserve` | receiver | receive buffer, late/dropped packets |
| `hybrid` | both | combination |

Notation: `s` MSS in bytes, `R` RTT in s, `p` loss rate, `T` delivered
throughput in bit/s (sender side: unique send rate * (1 - p)), `C` link capacity reported by the transport in bit/s.
Unless noted otherwise, results are smoothed with
`E = E + smoothing * (x - E)`. The confidence rises to 1 after five samples.

### mathis

```
BW = 8 * s / R * constant / sqrt(max(p, min_loss_rate))
```

Parameters: `constant` = 1.22, `min_loss_rate` = 1e-4, `smoothing` = 0.3

- Pro: Simple, well known, needs only RTT, loss and MSS.
- Con: Models the fair share of a TCP flow, not the capacity. Without loss the
  result is determined by `min_loss_rate`. Reacts only after packets are lost.

### tfrc

```
BW = 8 * s / (R * sqrt(2bp/3) + t_RTO * 3 * sqrt(3bp/8) * p * (1 + 32p^2))
t_RTO = rto_factor * R,  b = packets_per_ack,  p = max(p, min_loss_rate)
```

Parameters: `packets_per_ack` = 1, `rto_factor` = 4, `min_loss_rate` = 1e-4,
`smoothing` = 0.3

- Pro: Standardized (RFC 5348). More realistic than Mathis at high loss.
- Con: Same TCP-model limits as Mathis. Uses the packet loss rate as an
  approximation of the loss event rate.

### aimd

```
p > loss_threshold:  BW = min(BW, decrease_factor * T)
otherwise:           BW = min(BW + increase_bps * dt, headroom * T)
BW = clamp(BW, min_bps, max bandwidth)
```

Parameters: `loss_threshold` = 0.02, `decrease_factor` = 0.85,
`increase_bps` = 250e3, `headroom` = 1.5, `min_bps` = 100e3 (no smoothing)

- Pro: Robust and easy to reason about. Probes upwards, backs off on loss.
- Con: Sawtooth behaviour. Loss is a late signal. Cannot exceed `headroom`
  times the current throughput, so capacity far above the send rate stays
  hidden.

### delay

```
q = R - R_base          (R_base: minimum RTT; at the end of each window of
                         base_rtt_window_s it becomes the window's minimum)
q <= low_threshold_ms:  BW = headroom * T
q >= high_threshold_ms: BW = backoff * T
otherwise:              BW = T
```

Parameters: `low_threshold_ms` = 10, `high_threshold_ms` = 50,
`headroom` = 1.25, `backoff` = 0.9, `base_rtt_window_s` = 30,
`smoothing` = 0.3

- Pro: Detects congestion before packets are lost.
- Con: Sensitive to RTT noise and route changes. Needs a reliable base RTT.
  Loses against loss-based flows on a shared bottleneck.

### link_capacity

```
BW = utilization * median(last window_size values of C) * (1 - p)
```

Parameters: `window_size` = 5 (integer, 1 to 31), `utilization` = 0.9,
`smoothing` = 0.3

- Pro: Can see capacity above the current send rate. Median removes outliers.
- Con: Relies on the transport's packet-pair estimate, which is noisy and may
  overestimate on shaped or policed links.

### send_buffer (sender only)

```
congested = d_buf > buffer_threshold_ms  or  d(d_buf)/dt > max_growth_rate
congested:            BW = backoff * T
C available:          BW = max(median(last capacity_window values of C), T)
otherwise:            BW = headroom * T
```

Parameters: `buffer_threshold_ms` = 100, `max_growth_rate` = 0.05,
`backoff` = 0.9, `headroom` = 1.2, `capacity_window` = 5, `smoothing` = 0.3

- Pro: Direct congestion signal for live streams: the buffer grows as soon as
  the input rate exceeds what the link carries.
- Con: Sender side only. Bursty input (e.g. large key frames) can fill the
  buffer without congestion.

### tsbpd_reserve (receiver only)

```
r = receive buffer delay / configured latency
congested = r < min_reserve  or  late share + drop share > late_threshold
result as for send_buffer
```

Parameters: `min_reserve` = 0.5, `late_threshold` = 0.01, `backoff` = 0.9,
`headroom` = 1.2, `capacity_window` = 5, `smoothing` = 0.3

- Pro: Measures what matters for live playback: timely delivery.
- Con: Receiver side only; the estimate must be sent back to the sender by the
  application. Reacts once the reserve is already shrinking.

### hybrid

```
BW = min(valid estimates of aimd, delay, link_capacity,
         send_buffer (sender), tsbpd_reserve (receiver))
```

Parameters: those of the components, prefixed with the component name, e.g.
`aimd.headroom` or `delay.smoothing`.

- Pro: Combines early signals (delay, buffers) with late ones (loss) and an
  upper bound from the link capacity.
- Con: Conservative, since the most pessimistic component wins. Many
  parameters. `mathis` and `tfrc` are not included.

## Shared channel

When several senders share one (radio) channel, per-flow estimates must not
be averaged: every flow sees the capacity of the whole channel, so each
sender would be granted the total. `SharedLinkAllocator` estimates the total
bandwidth at the receiver and distributes it.

```cpp
bwe::SharedLinkAllocator allocator(
    "hybrid", {{"aimd.loss_threshold", 0.05},
               {"delay.low_threshold_ms", 30.0},
               {"delay.high_threshold_ms", 100.0}});
allocator.AddFlow(1, {/*weight=*/1.0, /*min_bps=*/0.0, /*max_bps=*/6e6});
allocator.AddFlow(2);

// Periodically, e.g. every 500 ms:
allocator.Update(1, stats_of_flow_1);
allocator.Update(2, stats_of_flow_2);
allocator.Tick(now);
for (const bwe::FlowAllocation& a : allocator.Allocate()) {
  SendToSender(a.flow, a.target_bps);  // Feedback is up to the application.
}
```

The statistics of all flows are combined into one measurement:

| Value | Combination |
|---|---|
| Counters | Sum of the increases (flows joining later add no history) |
| RTT, link capacity | Median over the flows |
| Receive buffer reserve | Flow with the lowest reserve |

Flows without statistics for `flow_timeout` (default 3 s) are left out. The
total is distributed by weight (water-filling): flows reaching `max_bps` are
capped and the rest is shared among the others; `min_bps` is always granted.

The parameters above suit radio links: random loss below 5 % is not treated
as congestion and the delay thresholds tolerate jitter.

If the channel capacity fades quickly, the congestion signals react too late
(they appear only once a queue has built up). A tighter, faster link capacity
bound avoids late packets at the cost of throughput:

```cpp
{"link_capacity.utilization", 0.8},
{"link_capacity.window_size", 1.0},  // Median over the flows is enough.
{"link_capacity.smoothing", 1.0},
```

In simulation (capacity +-30 %, period 10 s) this reduced late packets from
about 10 % to 0.3 %, while a stable channel was used to about 85 % instead of
92 %.

Equal bit rates do not mean equal airtime: a sender with a poor radio link
uses more airtime per bit. Weights proportional to the link efficiency (e.g.
0.5 for a sender reaching half the channel rate) give every sender the same
airtime.

## Recording

`bwe::RecordingWriter` writes measurements, estimates and allocations as CSV
to any `std::ostream`; `bwe::ReadRecording()` reads them back.

```cpp
std::ofstream file("recording.csv");
bwe::RecordingWriter writer(file);
writer.WriteReceiver(now, flow, stats);
writer.WriteEstimate(now, std::nullopt, "hybrid", allocator.GetTotalEstimate());
for (const auto& allocation : allocator.Allocate()) {
  writer.WriteAllocation(now, allocation);
}
```

Format: the first line is `# bwe-recording v1`, the second holds the column
names, then one line per record. The column `record` names the type:

| `record` | Columns used |
|---|---|
| `sender`, `receiver` | `timestamp_us` and the measurement fields (see mapping below) |
| `estimate` | `algorithm`, `bits_per_second`, `confidence`, `valid`, `timestamp_us` |
| `allocation` | `flow`, `target_bps` |
| `truth` | `capacity_bps` (simulated data only) |

Every line has `time_us` (application time) and optionally `flow` (empty for
channel-wide records). Empty cells mean "not set". Times and durations are
integers in microseconds (`*_us`), rates are in bit/s. Numbers are written
independently of the global locale and read back exactly. When reading,
columns may be missing or reordered; malformed input throws
`std::invalid_argument` naming the line.

## Tools

Built with `BWE_BUILD_TOOLS` (default on for the top-level project).

**Test data generator** (C++, uses the simulators of the tests): runs the
closed loop of senders, shared channel and `SharedLinkAllocator` and records
receiver statistics, allocations, the total estimate and the true capacity.

```
bwe_generate_test_data --scenario fading --output fading.csv
    [--senders 3] [--duration 60] [--seed 7] [--capacity 12e6]
    [--algorithm hybrid] [--param aimd.loss_threshold=0.05 ...]
```

Scenarios: `overload`, `capacity_drop`, `radio_loss`, `jitter`,
`mixed_quality`, `fading`.

**Plot script** (Python 3 with matplotlib): rates, delays and loss shares
over time.

```
python3 tools/plot_recording.py recording.csv [--flow 1] [--output plot.png]
```

**Replay test:** every `*.csv` in `tests/data/` is replayed through the
`SharedLinkAllocator` by the unit tests. Add own recordings there; checks
against the true capacity only apply to recordings that contain it.

## Thread safety

All public classes may be used from several threads at the same time:

| Class | Guarantee |
|---|---|
| `EstimatorFactory` | All member functions; creators run outside the lock |
| `SenderEstimator`, `ReceiverEstimator` | `Update()`, `GetEstimate()` and `Reset()` of one instance |
| `SharedLinkAllocator` | All member functions, e.g. `Update()` from each connection's statistics thread and `Tick()`/`Allocate()` from a timer |
| `RecordingWriter` | Records never interleave; writes to the same stream from outside the writer are not synchronized |
| `ReadRecording()` | Stateless |

Own estimators override the protected `DoUpdate()`, `DoGetEstimate()` and
`DoReset()`; the base class calls them with its mutex held, so they never
run concurrently. Locks are always taken in the order allocator, then
estimator, so the library cannot deadlock. A failing mutex lock (only on
resource exhaustion) terminates the program, since the affected functions
are `noexcept`.

The library needs no thread library of its own; only the tests link
`Threads::Threads` for `std::thread`.

## Mapping from SRT statistics

Source: `SRT_TRACEBSTATS`. Use the `*Total` counters.
Convert `ms*` values to `bwe::Duration` and `mbps*` values to bit/s (× 1e6).

| Field | SRT |
|---|---|
| **CommonStats** | |
| `timestamp` | `msTimeStamp` |
| `rtt` | `msRTT` |
| `link_capacity_bps` | `mbpsBandwidth` |
| `mss_bytes` | `byteMSS` |
| **SenderMeasurement** | |
| `packets_sent` | `pktSentTotal` |
| `packets_sent_unique` | `pktSentUniqueTotal` |
| `packets_lost` | `pktSndLossTotal` |
| `packets_retransmitted` | `pktRetransTotal` |
| `packets_dropped` | `pktSndDropTotal` |
| `bytes_sent` | `byteSentTotal` |
| `bytes_sent_unique` | `byteSentUniqueTotal` |
| `bytes_retransmitted` | `byteRetransTotal` |
| `bytes_dropped` | `byteSndDropTotal` |
| `send_buffer_delay` | `msSndBuf` |
| `send_buffer_bytes` | `byteSndBuf` |
| `send_buffer_packets` | `pktSndBuf` |
| `flight_size_packets` | `pktFlightSize` |
| `congestion_window_packets` | `pktCongestionWindow` |
| `max_bandwidth_bps` | `mbpsMaxBW` |
| **ReceiverMeasurement** | |
| `packets_received` | `pktRecvTotal` |
| `packets_received_unique` | `pktRecvUniqueTotal` |
| `packets_lost` | `pktRcvLossTotal` |
| `packets_dropped` | `pktRcvDropTotal` |
| `packets_belated` | `pktRcvBelated` (accumulate) |
| `bytes_received` | `byteRecvTotal` |
| `bytes_received_unique` | `byteRecvUniqueTotal` |
| `bytes_lost` | `byteRcvLossTotal` |
| `bytes_dropped` | `byteRcvDropTotal` |
| `receive_buffer_delay` | `msRcvBuf` |
| `receive_buffer_bytes` | `byteRcvBuf` |
| `receive_buffer_packets` | `pktRcvBuf` |
| `tsbpd_delay` | `msRcvTsbPdDelay` |
| `reorder_distance_packets` | `pktReorderDistance` |
