#!/usr/bin/env python3
"""Plots a BandwidthEstimator recording written by bwe::RecordingWriter.

Usage:
  python3 plot_recording.py recording.csv [--flow ID ...] [--output file.png]
                                          [--title TEXT]

Three panels share the time axis:
  1. Rates: true capacity, channel estimates, delivered rates, targets and
     per-flow estimates.
  2. Delays: RTT and buffer delays.
  3. Shares: loss, drop and late packets.

Requires Python 3 and matplotlib.
"""

import argparse
import csv
import sys
from collections import defaultdict

FORMAT_LINE = "# bwe-recording v1"
MBIT = 1e6


def read_recording(path):
    with open(path, newline="", encoding="utf-8") as stream:
        first = stream.readline().strip()
        if first != FORMAT_LINE:
            sys.exit(f"{path}: not a bwe recording (expected '{FORMAT_LINE}')")
        return list(csv.DictReader(stream))


def number(row, column):
    value = row.get(column) or ""
    return float(value) if value else None


def seconds(row):
    return float(row["time_us"]) / 1e6


def deltas(rows, columns):
    """Yields (time, dt, {column: delta}) for consecutive rows.

    Intervals with a missing value, a decreasing counter (reset) or a
    non-increasing time are skipped.
    """
    for previous, current in zip(rows, rows[1:]):
        dt = seconds(current) - seconds(previous)
        if dt <= 0:
            continue
        values = {}
        for column in columns:
            before, after = number(previous, column), number(current, column)
            if before is None or after is None or after < before:
                values = None
                break
            values[column] = after - before
        if values is not None:
            yield seconds(current), dt, values


def first_present(rows, *columns):
    for column in columns:
        if rows and all(number(row, column) is not None for row in rows):
            return column
    return None


def share(numerator, denominator):
    return 100.0 * numerator / denominator if denominator > 0 else None


class Series:
    def __init__(self):
        self.times = []
        self.values = []

    def add(self, time, value):
        if value is not None:
            self.times.append(time)
            self.values.append(value)


def collect(rows, flows):
    """Groups the rows into named series per panel."""
    rates = defaultdict(Series)
    delays = defaultdict(Series)
    shares = defaultdict(Series)
    delivered_by_time = defaultdict(float)

    by_flow = defaultdict(lambda: defaultdict(list))
    for row in rows:
        kind = row["record"]
        flow = row.get("flow") or ""
        if flow and flows and int(flow) not in flows and kind != "receiver":
            continue
        if kind in ("sender", "receiver"):
            by_flow[kind][flow].append(row)
        elif kind == "estimate" and row.get("valid") == "1":
            label = f"estimate {row['algorithm']}"
            label += f" (flow {flow})" if flow else " (channel)"
            rates[label].add(seconds(row), number(row, "bits_per_second"))
        elif kind == "allocation":
            rates[f"target flow {flow}"].add(
                seconds(row), number(row, "target_bps"))
        elif kind == "truth":
            rates["true capacity"].add(
                seconds(row), number(row, "capacity_bps"))

    for flow, flow_rows in sorted(by_flow["receiver"].items()):
        shown = not flows or (flow and int(flow) in flows)
        bytes_column = first_present(
            flow_rows, "bytes_received_unique", "bytes_received")
        packets_column = first_present(
            flow_rows, "packets_received_unique", "packets_received")
        if bytes_column:
            for time, dt, d in deltas(flow_rows, [bytes_column]):
                rate = d[bytes_column] * 8 / dt
                delivered_by_time[time] += rate
                if shown:
                    rates[f"delivered flow {flow}"].add(time, rate)
        if not shown:
            continue
        for row in flow_rows:
            rtt = number(row, "rtt_us")
            delays[f"RTT flow {flow}"].add(
                seconds(row), rtt / 1000 if rtt is not None else None)
            buffer_delay = number(row, "receive_buffer_delay_us")
            delays[f"receive buffer flow {flow}"].add(
                seconds(row),
                buffer_delay / 1000 if buffer_delay is not None else None)
        if packets_column:
            # Lost and dropped packets never arrived: share of the expected.
            for column, label in (("packets_lost", "loss"),
                                  ("packets_dropped", "drop")):
                if first_present(flow_rows, column):
                    for time, _, d in deltas(
                            flow_rows, [packets_column, column]):
                        shares[f"{label} flow {flow}"].add(
                            time,
                            share(d[column], d[packets_column] + d[column]))
            if first_present(flow_rows, "packets_belated"):
                for time, _, d in deltas(
                        flow_rows, [packets_column, "packets_belated"]):
                    shares[f"late flow {flow}"].add(
                        time, share(d["packets_belated"], d[packets_column]))

    for flow, flow_rows in sorted(by_flow["sender"].items()):
        if flows and (not flow or int(flow) not in flows):
            continue
        bytes_column = first_present(
            flow_rows, "bytes_sent_unique", "bytes_sent")
        packets_column = first_present(
            flow_rows, "packets_sent_unique", "packets_sent")
        if bytes_column:
            for time, dt, d in deltas(flow_rows, [bytes_column]):
                rates[f"sent flow {flow}"].add(
                    time, d[bytes_column] * 8 / dt)
        for row in flow_rows:
            rtt = number(row, "rtt_us")
            delays[f"RTT sender flow {flow}"].add(
                seconds(row), rtt / 1000 if rtt is not None else None)
            buffer_delay = number(row, "send_buffer_delay_us")
            delays[f"send buffer flow {flow}"].add(
                seconds(row),
                buffer_delay / 1000 if buffer_delay is not None else None)
        if packets_column and first_present(flow_rows, "packets_lost"):
            for time, _, d in deltas(
                    flow_rows, [packets_column, "packets_lost"]):
                shares[f"loss sender flow {flow}"].add(
                    time, share(d["packets_lost"], d[packets_column]))

    if len(by_flow["receiver"]) > 1:
        for time in sorted(delivered_by_time):
            rates["delivered, all flows"].add(time, delivered_by_time[time])
    return rates, delays, shares


def style(label):
    if label == "true capacity":
        return {"color": "black", "linestyle": "--", "linewidth": 1.5}
    if label.endswith("(channel)"):
        return {"linewidth": 2.0}
    if label == "delivered, all flows":
        return {"color": "gray", "linewidth": 2.0}
    if label.startswith("target"):
        return {"linestyle": ":", "linewidth": 1.2}
    return {"linewidth": 1.0}


def plot(rows, flows, output, title):
    import matplotlib
    if output:
        matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    rates, delays, shares = collect(rows, flows)
    figure, axes = plt.subplots(3, 1, sharex=True, figsize=(12, 9))
    panels = (
        (axes[0], rates, "Rate [Mbit/s]", 1 / MBIT),
        (axes[1], delays, "Delay [ms]", 1.0),
        (axes[2], shares, "Share [%]", 1.0),
    )
    for axis, series, label, scale in panels:
        for name, data in sorted(series.items()):
            if data.times:
                axis.plot(data.times, [v * scale for v in data.values],
                          label=name, **style(name))
        axis.set_ylabel(label)
        axis.grid(True, alpha=0.3)
        if axis.lines:
            axis.legend(loc="upper left", fontsize="small", ncol=2)
    axes[2].set_xlabel("Time [s]")
    figure.suptitle(title)
    figure.tight_layout()

    if output:
        figure.savefig(output, dpi=120)
    else:
        plt.show()
    return figure


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("recording", help="CSV file written by RecordingWriter")
    parser.add_argument("--flow", type=int, action="append", default=[],
                        help="show only this flow (repeatable)")
    parser.add_argument("--output", help="save to this image file instead "
                        "of opening a window")
    parser.add_argument("--title", help="title of the figure")
    args = parser.parse_args(argv)

    rows = read_recording(args.recording)
    plot(rows, set(args.flow), args.output, args.title or args.recording)
    return 0


if __name__ == "__main__":
    sys.exit(main())
