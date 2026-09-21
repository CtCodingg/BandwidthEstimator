# BandwidthEstimator

Small C++17 library that estimates the rate each sender may use. The estimation runs on the
receiver side; the result is sent back to the sender, which adjusts its rate.

- N streams (senders) sharing one channel per estimator, weighted split of its capacity
- Runs a background thread: push individual measurements any time, poll all results any time,
  no callbacks
- Exchangeable algorithm (`IAlgorithm` + `AlgorithmFactory`), currently TFRC
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
on a fixed interval (`Config::updateIntervalMs`, default 100 ms). Outputs are polled on demand
and always contain the latest result for every currently known stream.

```cpp
#include "bwe/Estimator.hpp"

bwe::Config config;                 // TFRC, packet size 1316 bytes, recalculates every 100 ms
bwe::Estimator estimator(config);   // starts the background thread

// whenever you get a new channel-level measurement (rtt/loss), independent of the streams
estimator.updateChannel(/* rttMs */ 80.0, /* dropRatePercent */ 0.5);

// whenever you get a new measurement for one stream; adds it if it is not known yet
bwe::StreamInput stream;
stream.streamId = 1;
stream.receiveRateBps = 4e6;
stream.weight = 1.0;                // share of the channel relative to the other streams
estimator.updateStream(stream);

// when a sender disconnects, frees its share of the channel for the others
estimator.removeStream(1);

// whenever you need the current results, e.g. right before sending
for (const bwe::Output& output : estimator.outputs())
{
	sendToSender(output.streamId, output.rateBps);
}
```

A custom algorithm implements `bwe::IAlgorithm::estimate()` and is passed as
`bwe::Estimator(std::make_unique<MyAlgorithm>(), updateIntervalMs)`.

An exception thrown by the algorithm during a background recalculation is swallowed and the
previous outputs are kept; it never terminates the program.

## Record and replay

```cpp
std::ofstream file("session.csv");
bwe::Recorder recorder(file);
recorder.recordChannel(rttMs, dropRatePercent);   // next to every updateChannel() call
recorder.recordStream(stream);                    // next to every updateStream() call
recorder.recordRemove(streamId);                  // next to every removeStream() call

// later
std::ifstream in("session.csv");
bwe::Player player(in);
bwe::Estimator simulation(config);                // other algorithm or settings possible
player.replay(simulation);                        // feeds every recorded event, in order
// poll simulation.outputs() afterwards for the result
```

Format: `step,kind,streamId,rttMs,dropRatePercent,receiveRateBps,weight`, one line per event,
`kind` is `channel`, `stream` or `remove`. Since the estimator recalculates on its own background
thread against the wall clock, replay reproduces the recorded *inputs* exactly but, unlike a
purely stateless estimator, cannot promise bit-identical outputs.

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

| Exception               | When                                                                     |
|--------------------------|----------------------------------------------------------------------------|
| `std::invalid_argument` | packet size 0, null algorithm, `updateIntervalMs` 0, input value out of range |
| `std::runtime_error`    | Recorder cannot write, Player reads an invalid file                       |

Valid input: `rttMs > 0`, `0 <= dropRatePercent <= 100`, every stream's `receiveRateBps >= 0` and
`weight > 0`, all finite.

## Algorithm: TFRC

TCP-Friendly Rate Control (RFC 5348) gives the rate a TCP connection would reach under the
same round-trip time and loss:

```
X = s / ( R·sqrt(2p/3) + t_RTO · 3·sqrt(3p/8) · p · (1 + 32p²) )

s     packet size in bytes (Config::packetSizeBytes)
R     round-trip time in seconds (rttMs / 1000)
p     drop rate as fraction (dropRatePercent / 100)
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

## SRT mapping

| Input            | SRT statistics (receiver, per interval)                 |
|------------------|---------------------------------------------------------|
| `rttMs`          | `msRTT`                                                 |
| `dropRatePercent`| `100 · pktRcvLoss / (pktRecv + pktRcvLoss)`             |
| `receiveRateBps` | `mbpsRecvRate · 1e6`                                    |
| `packetSizeBytes`| `SRTO_PAYLOADSIZE` (default 1316)                       |
