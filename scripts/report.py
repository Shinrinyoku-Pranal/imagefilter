#!/usr/bin/env python3
"""Check raw benchmark samples and print a Markdown results table."""

import argparse
import csv
from collections import defaultdict
import json
import math
from pathlib import Path
import statistics


def read_csv(path):
    with path.open(newline="") as stream:
        return list(csv.DictReader(stream))


def make_report(directory):
    rows = read_csv(directory / "benchmark.csv")
    samples = read_csv(directory / "benchmark_samples.csv")
    if not rows:
        raise ValueError("benchmark summary is empty")
    environment = directory / "environment.json"
    if environment.exists() and json.loads(environment.read_text()).get("exit_code") != 0:
        raise ValueError("benchmark did not finish successfully")
    grouped = defaultdict(list)
    for sample in samples:
        grouped[(sample["filter"], sample["threads"])].append(sample)
    seen = set()
    checksums = {}
    for row in rows:
        key = row["filter"], row["threads"]
        if key in seen:
            raise ValueError(f"duplicate configuration: {key}")
        seen.add(key)
        pairs = grouped[key]
        if len(pairs) != int(row["runs"]) or not pairs or row["verified"] != "true":
            raise ValueError(f"missing or unverified samples: {key}")
        if {int(pair["run"]) for pair in pairs} != set(range(1, len(pairs) + 1)):
            raise ValueError(f"missing or repeated run numbers: {key}")
        serial = statistics.median(float(pair["serial_ms"]) for pair in pairs)
        pool = statistics.median(float(pair["pool_ms"]) for pair in pairs)
        if serial <= 0 or pool <= 0:
            raise ValueError(f"timings must be positive: {key}")
        # CSV times have six decimal places in milliseconds. Very short runs
        # need a rounding allowance when reconstructing their speedup ratio.
        speedup = serial / pool
        rounding_error = 1e-6 + 1e-6 * (1 + speedup) / pool
        if not (math.isclose(serial, float(row["serial_median_ms"]), abs_tol=1e-6)
                and math.isclose(pool, float(row["pool_median_ms"]), abs_tol=1e-6)
                and math.isclose(speedup, float(row["speedup"]), rel_tol=0, abs_tol=rounding_error)):
            raise ValueError(f"summary differs from raw samples: {key}")
        ordered = sorted(pairs, key=lambda pair: int(pair["run"]))
        orders = [pair["order"] for pair in ordered]
        if any(order not in {"serial_first", "pool_first"} for order in orders) or any(
                left == right for left, right in zip(orders, orders[1:])):
            raise ValueError(f"execution order did not alternate: {key}")
        expected = checksums.setdefault(row["filter"], row["checksum"])
        if row["checksum"] != expected:
            raise ValueError(f"output checksum differs across worker counts: {row['filter']}")
    if seen != set(grouped):
        raise ValueError("raw samples contain unrecognized configurations")

    first = rows[0]
    lines = ["# Benchmark results", "",
             f"Input: {first['input_width']}×{first['input_height']} RGB. "
             f"Verified {len(samples)} timing pairs across {len(rows)} configurations.", "",
             "Times are processing medians; image I/O and pool startup are excluded.", "",
             "| Filter | Workers | Serial ms | Pool ms | Speedup |",
             "| --- | ---: | ---: | ---: | ---: |"]
    for row in rows:
        lines.append(f"| {row['filter']} | {row['threads']} | "
                     f"{float(row['serial_median_ms']):.2f} | {float(row['pool_median_ms']):.2f} | "
                     f"{float(row['speedup']):.2f}× |")
    return "\n".join(lines) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path, nargs="?", default=Path("results"))
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    try:
        report = make_report(args.directory)
    except (OSError, ValueError, KeyError) as error:
        parser.exit(1, f"Error: {error}\n")
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(report)
        print(f"Verified timing samples; wrote {args.output}")
    else:
        print(report, end="")


if __name__ == "__main__":
    main()
