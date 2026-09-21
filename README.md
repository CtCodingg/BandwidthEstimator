# BandwidthEstimator

Small C++17 library that estimates the rate each sender may use. The estimation runs on the
receiver side; the result is sent back to the sender, which adjusts its rate.

- N streams (senders) per estimator, one result per stream
- Exchangeable algorithm (`IAlgorithm` + `AlgorithmFactory`), currently TFRC
- Recording of inputs and results as CSV, replay as simulation
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

```cpp
#include "bwe/Estimator.hpp"

bwe::Config config;                 // TFRC, packet size 1316 bytes
bwe::Estimator estimator(config);

// one callback per sender: forward the result over your feedback channel
estimator.subscribe(1, [](const bwe::Output& output)
	{
		sendToSender(output.streamId, output.rateBps);
	});

// for every new statistics sample of a stream
bwe::Input input;
input.streamId = 1;
input.rttMs = 80.0;
input.dropRatePercent = 0.5;
input.receiveRateBps = 4e6;
estimator.update(input);
```

A custom algorithm implements `bwe::IAlgorithm::estimate()` and is passed as
`bwe::Estimator(std::make_unique<MyAlgorithm>())`.

## Record and replay

```cpp
std::ofstream file("session.csv");
bwe::Recorder recorder(file);
estimator.setObserver([&recorder](const bwe::Input& input, const bwe::Output& output)
	{
		recorder.record(input, output);
	});

// later
std::ifstream in("session.csv");
bwe::Player player(in);
bwe::Estimator simulation(config);            // other algorithm or settings possible
std::vector<bwe::Output> results = player.replay(simulation);
// compare results with player.records()[i].output
```

Format: `sequence,streamId,rttMs,dropRatePercent,receiveRateBps,rateBps`, one line per estimate.
No timestamps are stored: TFRC is stateless, so replaying in file order gives identical results.

## Example

`examples/Example.cpp` links the library and runs a closed loop: three senders share a
10 Mbit/s channel, a simple channel model gives drop rate and RTT, the estimator calculates a
new rate per sender and the callbacks hand it back to the senders. Everything is recorded to
`example.csv` (or the file given as first argument) and replayed afterwards to check that the
results are identical.

```sh
./build/bin/BandwidthEstimatorExample [file.csv]
```

The output shows the typical TFRC behaviour: without drops the rates double per step, as soon
as the channel is overloaded the drops pull them down hard.

## Exceptions

| Exception               | When                                                                  |
|-------------------------|-----------------------------------------------------------------------|
| `std::invalid_argument` | packet size 0, null algorithm, empty callback, input value out of range |
| `std::runtime_error`    | Recorder cannot write, Player reads an invalid file                   |

Valid input: `rttMs > 0`, `0 <= dropRatePercent <= 100`, `receiveRateBps >= 0`, all finite.

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
