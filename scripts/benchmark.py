#!/usr/bin/env python3
"""Run the C++ benchmark and save its output and machine information."""

import argparse
from datetime import datetime
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess
import sys


PROJECT = Path(__file__).resolve().parents[1]


def machine_info():
    info = {"processor": platform.processor(), "logical_cpus": os.cpu_count()}
    if sys.platform == "darwin":
        try:
            result = subprocess.run(
                ["system_profiler", "SPHardwareDataType", "-json"],
                capture_output=True, text=True, check=True, timeout=20,
            )
            hardware = json.loads(result.stdout)["SPHardwareDataType"][0]
            # Keep specifications without the serial number or hardware UUID.
            for key in ["machine_model", "chip_type", "number_processors", "physical_memory"]:
                if key in hardware:
                    info[key] = hardware[key]
        except (OSError, subprocess.SubprocessError, ValueError, KeyError, IndexError):
            pass
    return info


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=PROJECT / "build/imagefilter")
    parser.add_argument("--output", type=Path, default=PROJECT / "results")
    source = parser.add_mutually_exclusive_group()
    source.add_argument("--size", default="3840x2160")
    source.add_argument("--input", type=Path)
    parser.add_argument("--threads", help="comma-separated counts; default: automatic hardware sweep")
    parser.add_argument("--runs", type=int, default=9)
    parser.add_argument("--warmup", type=int, default=2)
    args = parser.parse_args()
    if not args.binary.is_file():
        parser.error(f"executable not found: {args.binary}; run make first")
    if not 1 <= args.runs <= 101 or not 0 <= args.warmup <= 20:
        parser.error("runs must be 1–101 and warmup must be 0–20")
    args.output.mkdir(parents=True, exist_ok=True)

    command = [str(args.binary.resolve()), "benchmark", "--runs", str(args.runs),
               "--warmup", str(args.warmup), "--radius", "5", "--sigma", "2",
               "--csv", str((args.output / "benchmark.csv").resolve())]
    if args.threads:
        command += ["--threads", args.threads]
    command += (["--input", str(args.input.resolve())] if args.input
                else ["--generate", args.size])
    metadata = {
        "started_at": datetime.now().astimezone().isoformat(),
        "platform": platform.platform(),
        "machine": platform.machine(),
        "hardware": machine_info(),
        "binary_sha256": hashlib.sha256(args.binary.read_bytes()).hexdigest(),
        "command": command,
        "input": {"file": args.input.name} if args.input else {"generated_size": args.size},
    }
    try:
        version = subprocess.run(["c++", "--version"], capture_output=True, text=True, timeout=10)
        metadata["compiler_detected"] = version.stdout.splitlines()[0] if version.returncode == 0 else "unknown"
    except (OSError, subprocess.SubprocessError, IndexError):
        metadata["compiler_detected"] = "unknown"
    with (args.output / "benchmark.log").open("w") as log:
        process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                   text=True, bufsize=1)
        for line in process.stdout:
            print(line, end="", flush=True)
            log.write(line)
        result = process.wait()
    metadata["completed_at"] = datetime.now().astimezone().isoformat()
    metadata["exit_code"] = result
    (args.output / "environment.json").write_text(json.dumps(metadata, indent=2) + "\n")
    return result


if __name__ == "__main__":
    raise SystemExit(main())
