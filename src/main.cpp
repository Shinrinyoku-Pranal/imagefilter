#include "imagefilter/filters.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {
using namespace imagefilter;
using Options = std::map<std::string, std::string>;
using Clock = std::chrono::steady_clock;

const char* help = R"(ImageFilter 1.0 - C++17 image processing with a persistent thread pool

Usage:
  imagefilter generate --output input.png [--size 3840x2160]
  imagefilter apply --input input.jpg --output output.png --filter blur
                    [--mode serial|pool] [--threads 10]
                    [--radius 5] [--sigma 2] [--resize 1920x1080]
  imagefilter benchmark [--input input.jpg | --generate 3840x2160]
                        [--filter all|blur|edge|resize|pipeline]
                        [--threads 1,2,4,8,10] [--runs 9] [--warmup 2]
                        [--radius 5] [--sigma 2] [--resize 1920x1080]
                        [--csv results.csv]

Filters: blur (separable Gaussian), edge (Sobel), resize (bilinear),
         pipeline (blur -> edge -> resize).
Defaults: apply uses pool mode; resize dimensions default to half the input.
Input: PNG/JPEG/BMP/TGA/PNM. Output: PNG/JPEG/BMP/TGA/PPM. All data is 8-bit RGB;
input alpha is discarded. Dimensions <= 32768; maximum 64 megapixels.

Benchmark: median of paired runs with alternating execution order; image I/O,
pool startup, checksums and equality checks are excluded. Kernel setup and
buffer allocation are included. Every output must match the serial version
byte for byte. CSV also creates a companion *_samples.csv with every raw pair.
Threads means worker threads, not a guarantee of dedicated physical cores.
)";

Options parse(int argc, char** argv) {
    Options options;
    for (int i = 2; i < argc; i += 2) {
        const std::string key = argv[i];
        if (key.rfind("--", 0) != 0 || i + 1 >= argc)
            throw std::invalid_argument("expected --option value, got: " + key);
        if (!options.emplace(key, argv[i + 1]).second)
            throw std::invalid_argument("duplicate option: " + key);
    }
    return options;
}

void allowed(const Options& options, const std::set<std::string>& names) {
    for (const auto& item : options)
        if (!names.count(item.first)) throw std::invalid_argument("unknown option: " + item.first);
}

std::string get(const Options& options, const std::string& key, std::string fallback = "") {
    const auto found = options.find(key);
    return found == options.end() ? fallback : found->second;
}

std::string required(const Options& options, const std::string& key) {
    const auto value = get(options, key);
    if (value.empty()) throw std::invalid_argument("missing required option: " + key);
    return value;
}

int integer(const std::string& text, int minimum, int maximum, const std::string& name) {
    int value = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || value < minimum || value > maximum)
        throw std::invalid_argument(name + " must be an integer between " + std::to_string(minimum) + " and " + std::to_string(maximum));
    return value;
}

double sigma_value(const std::string& text) {
    std::size_t consumed = 0;
    double value = 0;
    try { value = std::stod(text, &consumed); }
    catch (...) { throw std::invalid_argument("invalid sigma"); }
    if (consumed != text.size() || !std::isfinite(value) || value < 0.01 || value > 128)
        throw std::invalid_argument("sigma must be finite and between 0.01 and 128");
    return value;
}

std::pair<int, int> dimensions(const std::string& text) {
    const auto separator = text.find('x');
    if (separator == std::string::npos) throw std::invalid_argument("size must have the form WIDTHxHEIGHT");
    const int width = integer(text.substr(0, separator), 1, 32768, "width");
    const int height = integer(text.substr(separator + 1), 1, 32768, "height");
    Image::checked_size(width, height);
    return {width, height};
}

int default_threads() {
    return static_cast<int>(std::clamp(std::thread::hardware_concurrency(), 1u, 256u));
}

std::string default_sweep() {
    const int hardware = default_threads();
    std::string result = "1";
    for (int n : {2, 4, 8}) if (n < hardware) result += "," + std::to_string(n);
    if (hardware > 1) result += "," + std::to_string(hardware);
    return result;
}

std::vector<int> thread_counts(const std::string& text) {
    std::vector<int> counts;
    std::size_t begin = 0;
    do {
        const auto end = text.find(',', begin);
        const int count = integer(text.substr(begin, end == std::string::npos ? end : end - begin), 1, 256, "threads");
        if (std::find(counts.begin(), counts.end(), count) != counts.end())
            throw std::invalid_argument("duplicate thread count");
        counts.push_back(count);
        if (end == std::string::npos) break;
        begin = end + 1;
    } while (true);
    return counts;
}

struct Settings {
    int radius;
    double sigma;
    int width;
    int height;
};

Settings settings(const Options& options, const Image& input) {
    const auto size = dimensions(get(options, "--resize", std::to_string(std::max(1, input.width() / 2)) +
                                                        "x" + std::to_string(std::max(1, input.height() / 2))));
    return {integer(get(options, "--radius", "5"), 0, 128, "radius"),
            sigma_value(get(options, "--sigma", "2")), size.first, size.second};
}

void validate_filter(const std::string& filter) {
    if (filter != "blur" && filter != "edge" && filter != "resize" && filter != "pipeline")
        throw std::invalid_argument("filter must be blur, edge, resize, or pipeline");
}

Image process(const Image& input, const std::string& filter, const Settings& cfg, ThreadPool* pool) {
    if (filter == "blur") return gaussian_blur(input, cfg.radius, cfg.sigma, pool);
    if (filter == "edge") return sobel_edges(input, pool);
    if (filter == "resize") return resize_bilinear(input, cfg.width, cfg.height, pool);
    return pipeline(input, cfg.width, cfg.height, cfg.radius, cfg.sigma, pool);
}

struct TimedImage { Image image; double ms; };

TimedImage timed(const Image& input, const std::string& filter, const Settings& cfg, ThreadPool* pool) {
    const auto start = Clock::now();
    auto output = process(input, filter, cfg, pool);
    const auto stop = Clock::now();
    return {std::move(output), std::chrono::duration<double, std::milli>(stop - start).count()};
}

double median(std::vector<double> values) {
    std::sort(values.begin(), values.end());
    const auto middle = values.size() / 2;
    return values.size() % 2 ? values[middle] : (values[middle - 1] + values[middle]) / 2;
}

int generate(const Options& options) {
    allowed(options, {"--output", "--size"});
    const auto filename = required(options, "--output");
    const auto size = dimensions(get(options, "--size", "3840x2160"));
    const auto image = generate_image(size.first, size.second);
    save_image(image, filename);
    std::cout << "Generated " << image.width() << 'x' << image.height() << " RGB image: " << filename << '\n';
    return 0;
}

int apply(const Options& options) {
    allowed(options, {"--input", "--output", "--filter", "--mode", "--threads", "--radius", "--sigma", "--resize"});
    const auto filter = required(options, "--filter");
    validate_filter(filter);
    const auto input_name = required(options, "--input");
    const auto output_name = required(options, "--output");
    const auto mode = get(options, "--mode", "pool");
    if (mode != "serial" && mode != "pool") throw std::invalid_argument("mode must be serial or pool");
    const int threads = integer(get(options, "--threads", std::to_string(default_threads())), 1, 256, "threads");
    if (mode == "serial" && options.count("--threads"))
        throw std::invalid_argument("--threads only applies to pool mode; omit it for serial mode");
    const auto input = load_image(input_name);
    const auto cfg = settings(options, input);
    std::unique_ptr<ThreadPool> pool;
    if (mode == "pool") pool = std::make_unique<ThreadPool>(static_cast<std::size_t>(threads));
    const auto result = timed(input, filter, cfg, pool.get());
    save_image(result.image, output_name);
    std::cout << std::fixed << std::setprecision(3) << filter << ": " << result.ms << " ms processing, "
              << mode << " (" << (pool ? threads : 1) << (pool ? " workers)" : " calling thread)")
              << ", " << result.image.width() << 'x' << result.image.height() << " -> " << output_name << '\n';
    return 0;
}

int benchmark(const Options& options) {
    allowed(options, {"--input", "--generate", "--filter", "--threads", "--runs", "--warmup", "--radius", "--sigma", "--resize", "--csv"});
    if (options.count("--input") && options.count("--generate"))
        throw std::invalid_argument("choose either --input or --generate");
    std::vector<std::string> filters{"blur", "edge", "resize", "pipeline"};
    const auto selected = get(options, "--filter", "all");
    if (selected != "all") { validate_filter(selected); filters = {selected}; }
    const auto counts = thread_counts(get(options, "--threads", default_sweep()));
    const int runs = integer(get(options, "--runs", "9"), 1, 101, "runs");
    const int warmup = integer(get(options, "--warmup", "2"), 0, 20, "warmup");
    const auto input = [&] {
        if (options.count("--input")) return load_image(required(options, "--input"));
        const auto size = dimensions(get(options, "--generate", "3840x2160"));
        return generate_image(size.first, size.second);
    }();
    const auto cfg = settings(options, input);
    std::ofstream csv, samples;
    if (options.count("--csv")) {
        const auto name = required(options, "--csv");
        const auto dot = name.find_last_of('.');
        const auto slash = name.find_last_of("/\\");
        const auto stem = dot != std::string::npos && (slash == std::string::npos || dot > slash) ? name.substr(0, dot) : name;
        csv.open(name);
        samples.open(stem + "_samples.csv");
        if (!csv || !samples) throw std::runtime_error("cannot open CSV outputs; check the destination directory");
        csv << "filter,input_width,input_height,output_width,output_height,threads,runs,warmup,radius,sigma,serial_median_ms,pool_median_ms,speedup,efficiency_percent,checksum,verified\n" << std::fixed << std::setprecision(6);
        samples << "filter,threads,run,order,serial_ms,pool_ms\n" << std::fixed << std::setprecision(6);
    }
    std::cout << "Input: " << input.width() << 'x' << input.height() << " RGB, checksum " << input.checksum()
              << (options.count("--input") ? " (file)\n" : " (deterministic generated image)\n")
              << "Reported hardware concurrency: " << std::thread::hardware_concurrency() << '\n'
              << "Runs: " << runs << ", warmups: " << warmup << ", blur radius: " << cfg.radius << ", sigma: " << cfg.sigma
              << ", resize: " << cfg.width << 'x' << cfg.height << '\n'
              << "Median processing times include allocations; exclude image I/O and pool startup.\n"
              << "Every timed output is compared byte for byte outside the timed region.\n\n"
              << std::left << std::setw(12) << "Filter" << std::right << std::setw(9) << "Workers"
              << std::setw(14) << "Serial ms" << std::setw(14) << "Pool ms" << std::setw(12) << "Speedup"
              << std::setw(12) << "Efficiency" << '\n' << std::fixed << std::setprecision(3);
    std::size_t group = 0;
    for (const int workers : counts) {
        ThreadPool pool(static_cast<std::size_t>(workers));
        // Keep this pool alive across all filters and repetitions for this count.
        for (const auto& filter : filters) {
            for (int i = 0; i < warmup; ++i) {
                const auto serial = process(input, filter, cfg, nullptr);
                const auto parallel = process(input, filter, cfg, &pool);
                if (!(serial == parallel)) throw std::runtime_error("warmup output mismatch: " + filter);
            }
            std::vector<double> serial_times, pool_times;
            std::uint64_t checksum = 0;
            for (int run = 0; run < runs; ++run) {
                const bool serial_first = (static_cast<std::size_t>(run) + group) % 2 == 0;
                auto first = timed(input, filter, cfg, serial_first ? nullptr : &pool);
                auto second = timed(input, filter, cfg, serial_first ? &pool : nullptr);
                if (!(first.image == second.image))
                    throw std::runtime_error("timed output mismatch: " + filter + ", workers=" + std::to_string(workers));
                checksum = first.image.checksum();
                const double serial_ms = serial_first ? first.ms : second.ms;
                const double pool_ms = serial_first ? second.ms : first.ms;
                serial_times.push_back(serial_ms); pool_times.push_back(pool_ms);
                if (samples.is_open()) samples << filter << ',' << workers << ',' << run + 1 << ','
                    << (serial_first ? "serial_first" : "pool_first") << ',' << serial_ms << ',' << pool_ms << '\n';
            }
            const double serial_ms = median(serial_times), pool_ms = median(pool_times);
            const double speedup = serial_ms / pool_ms;
            std::cout << std::left << std::setw(12) << filter << std::right << std::setw(9) << workers
                      << std::setw(14) << serial_ms << std::setw(14) << pool_ms << std::setw(11) << speedup << 'x'
                      << std::setw(11) << 100 * speedup / workers << '%' << std::endl;
            if (csv.is_open()) {
                const bool resized = filter == "resize" || filter == "pipeline";
                csv << filter << ',' << input.width() << ',' << input.height() << ','
                    << (resized ? cfg.width : input.width()) << ',' << (resized ? cfg.height : input.height()) << ','
                    << workers << ',' << runs << ',' << warmup << ',' << cfg.radius << ',' << cfg.sigma << ','
                    << serial_ms << ',' << pool_ms << ',' << speedup << ',' << 100 * speedup / workers << ',' << checksum << ",true\n";
            }
            ++group;
        }
    }
    if (csv.is_open()) {
        csv.close(); samples.close();
        if (csv.fail() || samples.fail()) throw std::runtime_error("failed while writing benchmark CSV files");
        std::cout << "\nSaved summary and raw samples: " << get(options, "--csv") << '\n';
    }
    return 0;
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc < 2 || (argc == 2 && (std::string(argv[1]) == "--help" || std::string(argv[1]) == "help"))) {
            std::cout << help;
            return 0;
        }
        if (argc == 2 && std::string(argv[1]) == "--version") { std::cout << "ImageFilter 1.0.0\n"; return 0; }
        const std::string command = argv[1];
        const auto options = parse(argc, argv);
        if (command == "generate") return generate(options);
        if (command == "apply") return apply(options);
        if (command == "benchmark") return benchmark(options);
        throw std::invalid_argument("unknown command: " + command);
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << "\nRun imagefilter --help for usage.\n";
        return 1;
    }
}
