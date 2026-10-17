"""Check the benchmark wrapper and ensure reporting catches modified samples."""

import csv
import json
from pathlib import Path
import subprocess
import sys
import tempfile


project = Path(__file__).resolve().parents[1]
binary = Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix="imagefilter-scripts-") as folder:
    output = Path(folder)
    command = [sys.executable, str(project / "scripts/benchmark.py"), "--binary", str(binary),
               "--size", "113x67", "--threads", "1,3", "--runs", "3", "--warmup", "1", "--output", str(output)]
    run = subprocess.run(command, capture_output=True, text=True, timeout=45)
    assert run.returncode == 0, (run.stdout, run.stderr)
    metadata = json.loads((output / "environment.json").read_text())
    assert metadata["exit_code"] == 0 and len(metadata["binary_sha256"]) == 64
    assert metadata["input"] == {"generated_size": "113x67"}
    report_command = [sys.executable, str(project / "scripts/report.py"), str(output)]
    report = subprocess.run(report_command, capture_output=True, text=True, timeout=10)
    assert report.returncode == 0 and "Verified 24 timing pairs across 8 configurations" in report.stdout
    written = output / "report.md"
    subprocess.run([*report_command, "--output", str(written)], check=True, capture_output=True, text=True, timeout=10)
    assert written.read_text() == report.stdout
    summary = output / "benchmark.csv"
    original_summary = summary.read_text()
    summary.write_text(original_summary.replace(",true\n", ",false\n", 1))
    rejected = subprocess.run(report_command, capture_output=True, text=True, timeout=10)
    assert rejected.returncode != 0 and "unverified" in rejected.stderr
    summary.write_text(original_summary)
    samples_file = output / "benchmark_samples.csv"
    with samples_file.open(newline="") as stream:
        samples = list(csv.DictReader(stream))
    first_key = samples[0]["filter"], samples[0]["threads"]
    for sample in samples:
        if (sample["filter"], sample["threads"]) == first_key:
            sample["serial_ms"] = str(float(sample["serial_ms"]) + 1)
    with samples_file.open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(samples[0]))
        writer.writeheader()
        writer.writerows(samples)
    rejected = subprocess.run(report_command, capture_output=True, text=True, timeout=10)
    assert rejected.returncode != 0 and "summary differs" in rejected.stderr

print("PASS benchmark/report scripts: metadata, raw samples, report output, rejected unverified and changed data")
