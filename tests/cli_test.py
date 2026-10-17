"""End-to-end file format, command validation and benchmark integrity checks."""
import csv
import math
import pathlib
import statistics
import subprocess
import sys
import tempfile

binary = str(pathlib.Path(sys.argv[1]).resolve())
checks = 0


def run(*args, ok=True):
    global checks
    completed = subprocess.run([binary, *map(str, args)], capture_output=True, text=True, timeout=60)
    assert (completed.returncode == 0) == ok, (args, completed.stdout, completed.stderr)
    checks += 1
    return completed


def read_ppm(path):
    with path.open("rb") as stream:
        assert stream.readline() == b"P6\n"
        size = tuple(map(int, stream.readline().split()))
        assert stream.readline() == b"255\n"
        data = stream.read()
    assert len(data) == size[0] * size[1] * 3
    return size, data


with tempfile.TemporaryDirectory(prefix="imagefilter-test-") as folder:
    root = pathlib.Path(folder)
    original = root / "input.ppm"
    run("generate", "--output", original, "--size", "37x29")
    for extension in ["png", "bmp", "tga", "ppm", "jpg", "jpeg"]:
        encoded = root / ("encoded." + extension)
        decoded = root / (extension + "-decoded.ppm")
        run("apply", "--input", original, "--output", encoded, "--filter", "blur", "--radius", "0", "--mode", "serial")
        run("apply", "--input", encoded, "--output", decoded, "--filter", "blur", "--radius", "0", "--mode", "pool", "--threads", "3")
        before, after = read_ppm(original), read_ppm(decoded)
        assert before[0] == after[0]
        if extension in {"jpg", "jpeg"}:
            assert sum(abs(a - b) for a, b in zip(before[1], after[1])) / len(before[1]) < 15
        else:
            assert before == after, extension
    for name in ["blur", "edge", "resize", "pipeline"]:
        serial, pool = root / (name + "-s.png"), root / (name + "-p.png")
        base = ["apply", "--input", original, "--filter", name, "--resize", "19x13"]
        run(*base, "--output", serial, "--mode", "serial")
        run(*base, "--output", pool, "--mode", "pool", "--threads", "4")
        assert serial.read_bytes() == pool.read_bytes(), name
    gray = root / "gray.pgm"
    gray.write_bytes(b"P5\n2 2\n255\n" + bytes([0, 64, 128, 255]))
    gray_rgb = root / "gray.ppm"
    run("apply", "--input", gray, "--output", gray_rgb, "--filter", "blur", "--radius", "0")
    assert read_ppm(gray_rgb)[1] == bytes([0] * 3 + [64] * 3 + [128] * 3 + [255] * 3)
    corrupt = root / "bad.png"
    corrupt.write_bytes(b"not an image")
    run("apply", "--input", corrupt, "--output", root / "bad-out.png", "--filter", "blur", ok=False)
    for args in [
        ["apply"],
        ["apply", "--input", original, "--output", root / "x.png", "--filter", "nope"],
        ["apply", "--input", original, "--output", root / "x.png", "--filter", "blur", "--sigma", "nan"],
        ["apply", "--input", original, "--output", root / "x.png", "--filter", "blur", "--threads", "0"],
        ["apply", "--input", original, "--output", root / "x.png", "--filter", "blur", "--mode", "serial", "--threads", "4"],
        ["apply", "--input", root / "missing.png", "--output", root / "x.png", "--filter", "blur"],
        ["generate", "--output", root / "x.gif"],
        ["generate", "--output", root / "x.png", "--size", "32768x32768"],
        ["generate", "--output", root / "x.png", "--size", "-1x3"],
        ["generate", "--output", root / "x.png", "--size", "12x13x14"],
        ["generate", "--output", root / "x.png", "--unknown", "2"],
        ["generate", "--output", root / "x.png", "--size", "3x3", "--size", "4x4"],
        ["generate", "--output"],
        ["benchmark", "--generate", "4x4", "--threads", "1,"],
        ["benchmark", "--generate", "4x4", "--threads", "2,2"],
        ["benchmark", "--generate", "4x4", "--input", original],
        ["benchmark", "--generate", "4x4", "--runs", "0"],
        ["benchmark", "--generate", "4x4", "--runs", "1x"],
    ]:
        run(*args, ok=False)
    summary = root / "benchmark.csv"
    run("benchmark", "--generate", "113x67", "--resize", "57x31", "--threads", "1,3", "--runs", "3", "--warmup", "1", "--csv", summary)
    rows = list(csv.DictReader(summary.open()))
    samples = list(csv.DictReader((root / "benchmark_samples.csv").open()))
    assert len(rows) == 8 and len(samples) == 24
    for row in rows:
        matching = [sample for sample in samples if (sample["filter"], sample["threads"]) == (row["filter"], row["threads"])]
        serial = statistics.median(float(s["serial_ms"]) for s in matching)
        pool = statistics.median(float(s["pool_ms"]) for s in matching)
        assert math.isclose(float(row["serial_median_ms"]), serial, abs_tol=1e-6)
        assert math.isclose(float(row["pool_median_ms"]), pool, abs_tol=1e-6)
        assert math.isclose(float(row["speedup"]), serial / pool, rel_tol=0.01)
        assert row["verified"] == "true"
        assert len({s["order"] for s in matching}) == 2
        for other in rows:
            if row["filter"] == other["filter"]:
                assert row["checksum"] == other["checksum"]
    # Exercise a loaded-input benchmark too.
    run("benchmark", "--input", original, "--filter", "pipeline", "--threads", "2", "--runs", "1", "--warmup", "0")
    run("--help")
    run("--version")

print(f"PASS CLI integration: {checks} invocations, six format round-trips, exact serial/pool outputs, raw benchmark integrity")
