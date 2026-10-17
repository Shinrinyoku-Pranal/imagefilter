# Benchmark notes

The recorded run used a generated **3840×2160 RGB image** with gradients, noise, a checkerboard, and a disk. All four operations ran with 1, 2, 4, 8, and 10 workers: 20 configurations total.

Machine: Apple M4 MacBook Pro, 10 CPU cores (4 performance + 6 efficiency), 16 GB RAM, macOS 27.0.1. Built with Apple Clang 21.0.0 and Make using `-O3 -DNDEBUG -std=c++17 -Wall -Wextra -Wpedantic -pthread`. Recorded on October 17, 2026.

## Times with 10 workers

| Operation | Serial median | Pool median | Speedup |
| --- | ---: | ---: | ---: |
| Gaussian blur | 267.93 ms | 37.41 ms | 7.16× |
| Sobel edges | 21.41 ms | 5.04 ms | 4.25× |
| Bilinear resize | 12.21 ms | 1.91 ms | 6.41× |
| Full pipeline | 300.26 ms | 43.86 ms | **6.85×** |

## Scaling

Speedup is the single-threaded median divided by the pool median. Each configuration has its own paired serial measurements.

| Workers | Blur | Edges | Resize | Pipeline |
| ---: | ---: | ---: | ---: | ---: |
| 1 | 0.99× | 0.99× | 1.01× | 0.99× |
| 2 | 1.93× | 1.87× | 1.97× | 1.94× |
| 4 | 3.71× | 3.13× | 3.87× | 3.72× |
| 8 | 6.16× | 4.03× | 5.73× | 5.97× |
| 10 | 7.16× | 4.25× | 6.41× | 6.85× |

The one-worker pool stays close to the direct serial path. Scaling slows as more workers are added. The processor has mixed core types, and workers also share memory bandwidth.

## Method

- Blur radius: 5; sigma: 2. Resize output: 1920×1080.
- Two warmup pairs, followed by nine timed serial/parallel pairs per configuration: **180 timed pairs** total.
- Serial/pool execution order alternates between runs.
- Both modes use the same filter implementation. A null pool runs the work directly on the calling thread.
- Timings include allocations, filter setup, processing, queue dispatch, and synchronization. They exclude image generation/loading, saving, pool startup/shutdown, checksums, and output comparison. Final output destruction is also outside the timed region.
- Every warmup and timed pair is compared byte for byte. All pairs in this run matched, and output checksums agreed across worker counts.

This is a processing benchmark on one generated image. Application startup and file I/O can reduce the speedup seen when processing a single file. The scheduler, input size, filter settings, and other work on the machine also affect results.

## Data and reproduction

The [summary](apple-m4/benchmark.csv), [all timing pairs](apple-m4/benchmark_samples.csv), [console output](apple-m4/benchmark.log), [environment](apple-m4/environment.json), and [build information](apple-m4/build_info.json) are included. Local paths in the published log and metadata were changed to relative paths; the CSV measurements are unchanged. The [source hashes](apple-m4/source_hashes.json) identify the C++ files used for this run.

From the repository root:

```sh
# Check the recorded statistics without running the filters again.
python3 scripts/report.py benchmarks/apple-m4

# Build and collect a new run in results/.
make -j4
python3 scripts/benchmark.py --threads 1,2,4,8,10 --runs 9 --warmup 2
python3 scripts/report.py results --output results/report.md
```

The original release and sanitizer builds both passed eight C++ test groups and 45 CLI checks. See `make test` and `make sanitize` to run the current repository checks.
