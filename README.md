# BandwidthEstimator

Small C++17 library that estimates the rate each sender may use. The estimation runs on the
receiver side; the result is sent back to the sender, which adjusts its rate.

- N streams (senders) sharing one channel per estimator, weighted split of its capacity, each
  optionally capped at its own maximum rate
- Runs a background thread: push individual measurements any time, poll all results any time,
  no callbacks
- Exchangeable algorithm (`IAlgorithm` + `AlgorithmFactory`): TFRC, AIMD, or RTT-trend
- Recording of the estimator's inputs as CSV, replay as simulation
- Static library, thread-safe, no dependencies (GoogleTest only for the tests)
- Linux (CentOS 8, GCC 8) and Windows

## Build

```sh
cmake -S . -B build -DBWE_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build        # CMake >= 3.20, otherwise: cd build && ctest
```

| Option              | Default | Meaning                                   |
|---------------------|---------|-------------------------------------------|
| `BWE_BUILD_TESTS`   | `ON`    | `OFF` skips the tests and GoogleTest      |
| `BWE_BUILD_EXAMPLES`| `ON`    | `OFF` skips the example application      |

CentOS 8: `dnf install gcc-c++ cmake`, GoogleTest e.g. `gtest-devel` from EPEL.

## Usage

The estimator owns a background thread for its whole lifetime. Individual measurements are
pushed whenever they arrive, in any order, from any thread; the thread recalculates all outputs
on a fixed interval (`Config::update_interval_ms`, default 100 ms). Outputs are polled on demand
and always contain the latest result for every currently known stream.

```cpp
#include "bwe/Estimator.hpp"

bwe::Config config;                 // TFRC, packet size 1316 bytes, recalculates every 100 ms
bwe::Estimator estimator(config);   // starts the background thread

// whenever you get a new channel-level measurement (rtt/loss), independent of the streams
estimator.UpdateChannel(/* rtt_ms */ 80.0, /* drop_rate_percent */ 0.5);

// whenever you get a new measurement for one stream; adds it if it is not known yet
bwe::StreamInput stream;
stream.stream_id = 1;
stream.receive_rate_bps = 4e6;
stream.weight = 1.0;                // share of the channel relative to the other streams
stream.max_rate_bps = 8e6;          // optional cap, defaults to no limit (infinity)
estimator.UpdateStream(stream);

// when a sender disconnects, frees its share of the channel for the others
estimator.RemoveStream(1);

// a stream is also dropped automatically once it goes this long without an UpdateStream() call
// (Config::stream_timeout_ms, default 0 = disabled)

// whenever you need the current results, e.g. right before sending
for (const bwe::Output& output : estimator.Outputs())
{
	SendToSender(output.stream_id, output.rate_bps);
}
```

A custom algorithm implements `bwe::IAlgorithm::Estimate()` and is passed as
`bwe::Estimator(std::make_unique<MyAlgorithm>(), update_interval_ms, stream_timeout_ms)`.

An exception thrown by the algorithm during a background recalculation is swallowed and the
previous outputs are kept; it never terminates the program.

## Record and replay

```cpp
std::ofstream file("session.csv");
bwe::Recorder recorder(file);
recorder.RecordChannel(rtt_ms, drop_rate_percent);   // next to every UpdateChannel() call
recorder.RecordStream(stream);                       // next to every UpdateStream() call
recorder.RecordRemove(stream_id);                    // next to every RemoveStream() call
recorder.RecordOutput(output);                       // optional: a checkpoint, e.g. estimator.Outputs()
                                                      // once the session has settled

// later
std::ifstream in("session.csv");
bwe::Player player(in);
bwe::Estimator simulation(config);                   // other algorithm or settings possible
player.Replay(simulation);                           // feeds every recorded event, in order;
                                                      // RecordOutput() checkpoints are not fed back
                                                      // in, only Player::Events() exposes them
// poll simulation.Outputs() afterwards for the result
```

Format: `step,kind,streamId,rttMs,dropRatePercent,receiveRateBps,weight,maxRateBps,estimatedRateBps`,
one line per event, `kind` is `channel`, `stream`, `remove` or `output`. Since the estimator
recalculates on its own background thread against the wall clock, replay reproduces the recorded
*inputs* exactly but, unlike a purely stateless estimator, cannot promise bit-identical outputs -
which is also why `RecordOutput()` checkpoints are meant to be compared with a delta, not asserted
exactly.

### Recording fixtures as tests

Drop any `Recorder` output (a real one from production, or a hand-built scenario) as a `.csv` file
into `tests/fixtures/recordings/`; it is picked up automatically and replayed as its own named
GoogleTest test (`RecordingFixtureTest.<filename>`), no test code required. It always checks the
replay is structurally sane (settles on the right stream count, finite, non-negative rates); if the
recording also has `RecordOutput()` checkpoints, their values are compared against the live replay
within a relative delta. `realistic_session.csv` is the checked-in example, including checkpoints
for its final state. For a test that also asserts on that one recording explicitly, see
`RecordReplayTest.ReplaysARealisticRecordedSession`.

## Example

`examples/Example.cpp` links the library and runs a closed loop: three senders share a
10 Mbit/s channel (one of them weighted to get twice the share of the other two), a simple
channel model gives drop rate and RTT, and the estimator's result is polled and handed straight
back to the senders, no callbacks involved. Everything is recorded to `example.csv` (or the file
given as first argument) and replayed afterwards into a fresh estimator.

```sh
./build/examples/BandwidthEstimatorExample [file.csv]
```

The output shows the typical TFRC behaviour: without drops the rates double per step, as soon
as the channel is overloaded the drops pull them down hard.

## Exceptions

| Exception               | When                                                                          |
|--------------------------|-------------------------------------------------------------------------------|
| `std::invalid_argument` | packet size 0, null algorithm, `update_interval_ms` 0, input value out of range |
| `std::runtime_error`    | Recorder cannot write, Player reads an invalid file                            |

Valid input: `rtt_ms > 0`, `0 <= drop_rate_percent <= 100`, every stream's `receive_rate_bps >= 0`,
`weight > 0` and `max_rate_bps > 0` (default: no limit), all finite except `max_rate_bps`, which
may be infinite.

## Algorithm: TFRC

TCP-Friendly Rate Control (RFC 5348) gives the rate a TCP connection would reach under the
same round-trip time and loss:

```
X = s / ( R·sqrt(2p/3) + t_RTO · 3·sqrt(3p/8) · p · (1 + 32p²) )

s     packet size in bytes (Config::packet_size_bytes)
R     round-trip time in seconds (rtt_ms / 1000)
p     drop rate as fraction (drop_rate_percent / 100)
t_RTO 4·R
```

Result in bit/s: `rate = max( min(8·X, 2·receiveRate), 8·s / 64 )`

- `p = 0`: the equation has no limit, the rate is `2·receiveRate`
- upper limit `2·receiveRate`: the sender can at most double its rate per estimate
- lower limit: one packet per 64 seconds

Pros: smooth rate, fair against TCP and other TFRC streams on the same channel, cheap, no state.
Cons: reacts only after drops occur (no delay-based early detection); probes by doubling
while there are no drops; uses the plain drop rate instead of the RFC's loss event rate,
so bursty losses reduce the rate more than in RFC 5348.

## Algorithm: AIMD

Additive-Increase/Multiplicative-Decrease, the scheme classic TCP and RTP congestion control use.
Unlike TFRC it is stateful: every estimate builds on the previous one, seeded at `receiveRate` on
the first call.

- no drops: `rate += 8 · packet_size_bytes` (grows by one packet per call)
- any drop (`drop_rate_percent > 0`): `rate *= 0.5`, regardless of how large the drop rate is
- floor: one packet per 64 seconds, same as TFRC

Pros: simple, cheap, easy to reason about and test deterministically.
Cons: oscillates more than TFRC's smooth equation, is less fair against non-AIMD traffic, and
treats every drop the same regardless of severity (a 0.1% and a 50% drop rate cut the rate by the
same factor).

## Algorithm: RTT-trend

Delay-primary, loss-backstop rate control. TFRC and AIMD both treat any loss as a congestion
signal, which is the RFC 5348 / classic-TCP assumption for wired links - on a link where most loss
is corruption rather than queueing (radio, interference, fading), that assumption needlessly cuts
the rate on a link that is otherwise fine. This algorithm instead reacts primarily to RTT rising
above its observed baseline (i.e. actual queueing), and only falls back to loss as a backstop once
it gets severe - the case of a link with too little buffering to show queueing delay before it
drops.

Stateful, like AIMD: every estimate depends on the previous one and on the lowest RTT observed so
far (its baseline).

- baseline: `min_rtt = min(min_rtt, rttMs)`, never decreases
- overuse (`rttMs - min_rtt > 30`) or severe loss (`dropRatePercent > 10`): `rate *= 0.85`
- otherwise (including ordinary background loss): `rate += 8 · packet_size_bytes`
- floor: one packet per 64 seconds, same as TFRC and AIMD

Pros: tolerates the kind of loss a radio/wireless link produces under normal conditions instead of
needlessly backing off; reacts to real queueing before it turns into loss.
Cons: the 30 ms / 10% thresholds are tuned heuristics, not a standardized reference equation like
TFRC's - they may need adjusting per link; the RTT baseline only ever decreases, so a permanent
route change to a higher-latency path is read as sustained congestion until the process restarts.

## SRT mapping

| Input               | SRT statistics (receiver, per interval)                 |
|---------------------|----------------------------------------------------------|
| `rtt_ms`            | `msRTT`                                                  |
| `drop_rate_percent` | `100 · pktRcvLoss / (pktRecv + pktRcvLoss)`              |
| `receive_rate_bps`  | `mbpsRecvRate · 1e6`                                     |
| `packet_size_bytes` | `SRTO_PAYLOADSIZE` (default 1316)                        |
