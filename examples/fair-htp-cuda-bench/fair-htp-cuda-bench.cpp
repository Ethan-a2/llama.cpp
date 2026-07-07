#include "ggml.h"
#include "ggml-backend.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

struct bench_params {
    std::string backend = "cpu";
    std::string case_name = "mm_decode_f16";
    std::string json_path;
    int in_features = 1024;
    int out_features = 1024;
    int warmup = 10;
    int iterations = 100;
    uint64_t seed = 20260707;
    float abs_tol = 0.08f;
    float rel_tol = 0.05f;
    bool fail_on_skipped = false;
    bool fail_on_fallback = false;
    bool list_backends = false;
};

struct backend_handle {
    ggml_backend_t ptr = nullptr;
    ~backend_handle() {
        if (ptr) {
            ggml_backend_free(ptr);
        }
    }
};

struct buffer_handle {
    ggml_backend_buffer_t ptr = nullptr;
    ~buffer_handle() {
        if (ptr) {
            ggml_backend_buffer_free(ptr);
        }
    }
};

struct context_handle {
    ggml_context * ptr = nullptr;
    ~context_handle() {
        if (ptr) {
            ggml_free(ptr);
        }
    }
};

static void usage(const char * prog) {
    std::fprintf(stderr,
        "usage: %s [--backend cpu|cuda|htp|hexagon] [--case mm_decode_f16] [options]\n"
        "options:\n"
        "  --in N                 input features (default 1024)\n"
        "  --out N                output features (default 1024)\n"
        "  --warmup N             warmup iterations (default 10)\n"
        "  --iterations N         measured iterations (default 100)\n"
        "  --seed N               deterministic input seed (default 20260707)\n"
        "  --abs-tol X            absolute error tolerance (default 0.08)\n"
        "  --rel-tol X            relative error tolerance (default 0.05)\n"
        "  --json PATH            write JSON result to PATH\n"
        "  --fail-on-skipped      return non-zero when backend is unavailable\n"
        "  --fail-on-fallback     return non-zero when target backend does not support the graph\n"
        "  --list-backends        print registered backend devices\n",
        prog);
}

static bool parse_int(const char * text, int & out) {
    char * end = nullptr;
    long value = std::strtol(text, &end, 10);
    if (!end || *end != '\0' || value < 1 || value > INT32_MAX) {
        return false;
    }
    out = (int) value;
    return true;
}

static bool parse_u64(const char * text, uint64_t & out) {
    char * end = nullptr;
    unsigned long long value = std::strtoull(text, &end, 10);
    if (!end || *end != '\0') {
        return false;
    }
    out = (uint64_t) value;
    return true;
}

static bool parse_float(const char * text, float & out) {
    char * end = nullptr;
    float value = std::strtof(text, &end);
    if (!end || *end != '\0' || !std::isfinite(value) || value < 0.0f) {
        return false;
    }
    out = value;
    return true;
}

static bench_params parse_args(int argc, char ** argv) {
    bench_params params;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto need_value = [&](const char * name) -> const char * {
            if (i + 1 >= argc) {
                throw std::runtime_error(std::string("missing value for ") + name);
            }
            return argv[++i];
        };
        if (arg == "--backend") {
            params.backend = need_value("--backend");
        } else if (arg == "--case") {
            params.case_name = need_value("--case");
        } else if (arg == "--in") {
            if (!parse_int(need_value("--in"), params.in_features)) {
                throw std::runtime_error("invalid --in");
            }
        } else if (arg == "--out") {
            if (!parse_int(need_value("--out"), params.out_features)) {
                throw std::runtime_error("invalid --out");
            }
        } else if (arg == "--warmup") {
            if (!parse_int(need_value("--warmup"), params.warmup)) {
                throw std::runtime_error("invalid --warmup");
            }
        } else if (arg == "--iterations") {
            if (!parse_int(need_value("--iterations"), params.iterations)) {
                throw std::runtime_error("invalid --iterations");
            }
        } else if (arg == "--seed") {
            if (!parse_u64(need_value("--seed"), params.seed)) {
                throw std::runtime_error("invalid --seed");
            }
        } else if (arg == "--abs-tol") {
            if (!parse_float(need_value("--abs-tol"), params.abs_tol)) {
                throw std::runtime_error("invalid --abs-tol");
            }
        } else if (arg == "--rel-tol") {
            if (!parse_float(need_value("--rel-tol"), params.rel_tol)) {
                throw std::runtime_error("invalid --rel-tol");
            }
        } else if (arg == "--json") {
            params.json_path = need_value("--json");
        } else if (arg == "--fail-on-skipped") {
            params.fail_on_skipped = true;
        } else if (arg == "--fail-on-fallback") {
            params.fail_on_fallback = true;
        } else if (arg == "--list-backends") {
            params.list_backends = true;
        } else if (arg == "--help" || arg == "-h") {
            usage(argv[0]);
            std::exit(0);
        } else {
            throw std::runtime_error("unknown argument: " + arg);
        }
    }
    return params;
}

static std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return (char) std::tolower(c); });
    return value;
}

static bool contains_lower(const char * value, const char * needle) {
    if (!value) {
        return false;
    }
    return lower(value).find(needle) != std::string::npos;
}

static void list_backends(std::ostream & out) {
    ggml_backend_load_all();
    out << "registered backend devices:\n";
    for (size_t i = 0; i < ggml_backend_dev_count(); ++i) {
        ggml_backend_dev_t dev = ggml_backend_dev_get(i);
        out << "  " << i << ": name=" << ggml_backend_dev_name(dev)
            << " desc=" << ggml_backend_dev_description(dev)
            << " type=" << (int) ggml_backend_dev_type(dev) << "\n";
    }
}

static ggml_backend_dev_t find_device(const std::string & backend) {
    const std::string b = lower(backend);
    if (b == "cpu") {
        return ggml_backend_dev_by_type(GGML_BACKEND_DEVICE_TYPE_CPU);
    }

    for (size_t i = 0; i < ggml_backend_dev_count(); ++i) {
        ggml_backend_dev_t dev = ggml_backend_dev_get(i);
        const char * name = ggml_backend_dev_name(dev);
        const char * desc = ggml_backend_dev_description(dev);
        if ((b == "cuda" || b == "gpu") && (contains_lower(name, "cuda") || contains_lower(desc, "cuda"))) {
            return dev;
        }
        if ((b == "htp" || b == "hexagon") &&
            (contains_lower(name, "htp") || contains_lower(name, "hexagon") || contains_lower(desc, "hexagon"))) {
            return dev;
        }
    }
    return ggml_backend_dev_by_name(backend.c_str());
}

static uint64_t fnv1a64(const void * data, size_t size, uint64_t hash = 1469598103934665603ull) {
    const uint8_t * bytes = (const uint8_t *) data;
    for (size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= 1099511628211ull;
    }
    return hash;
}

static uint64_t next_u64(uint64_t & state) {
    state += 0x9e3779b97f4a7c15ull;
    uint64_t z = state;
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
}

static float next_uniform(uint64_t & state) {
    const uint64_t value = next_u64(state);
    const double unit = (double) (value >> 11) * (1.0 / 9007199254740992.0);
    return (float) (unit - 0.5);
}

static std::string hex_u64(uint64_t value) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%016llx", (unsigned long long) value);
    return std::string(buf);
}

static double percentile(std::vector<double> values, double p) {
    if (values.empty()) {
        return 0.0;
    }
    std::sort(values.begin(), values.end());
    const double pos = (values.size() - 1) * p;
    const size_t lo = (size_t) std::floor(pos);
    const size_t hi = (size_t) std::ceil(pos);
    if (lo == hi) {
        return values[lo];
    }
    const double w = pos - lo;
    return values[lo] * (1.0 - w) + values[hi] * w;
}

static std::string json_escape(const std::string & value) {
    std::string out;
    out.reserve(value.size() + 8);
    for (char c : value) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"':  out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:   out += c; break;
        }
    }
    return out;
}

static std::string status_json(const std::string & status, const std::string & reason, const bench_params & params) {
    std::ostringstream out;
    out << "{\n"
        << "  \"schema_version\": 1,\n"
        << "  \"case\": \"" << json_escape(params.case_name) << "\",\n"
        << "  \"backend\": \"" << json_escape(params.backend) << "\",\n"
        << "  \"status\": \"" << json_escape(status) << "\",\n"
        << "  \"reason\": \"" << json_escape(reason) << "\"\n"
        << "}\n";
    return out.str();
}

struct graph_case {
    context_handle ctx;
    ggml_tensor * weights = nullptr;
    ggml_tensor * input = nullptr;
    ggml_tensor * output = nullptr;
    ggml_cgraph * graph = nullptr;
    buffer_handle buffer;
};

static graph_case build_mm_case(ggml_backend_t backend, const bench_params & params) {
    const size_t ctx_size = 64ull * 1024ull * 1024ull;
    ggml_init_params init = { ctx_size, nullptr, true };
    graph_case gc;
    gc.ctx.ptr = ggml_init(init);
    if (!gc.ctx.ptr) {
        throw std::runtime_error("ggml_init failed");
    }

    gc.weights = ggml_new_tensor_2d(gc.ctx.ptr, GGML_TYPE_F16, params.in_features, params.out_features);
    gc.input   = ggml_new_tensor_2d(gc.ctx.ptr, GGML_TYPE_F16, params.in_features, 1);
    ggml_set_name(gc.weights, "weights_f16");
    ggml_set_name(gc.input, "input_f16");

    gc.output = ggml_mul_mat(gc.ctx.ptr, gc.weights, gc.input);
    ggml_set_name(gc.output, "output_f32");
    if (gc.output->type != GGML_TYPE_F32) {
        throw std::runtime_error("mm_decode_f16 output is not F32");
    }

    gc.graph = ggml_new_graph(gc.ctx.ptr);
    ggml_build_forward_expand(gc.graph, gc.output);

    gc.buffer.ptr = ggml_backend_alloc_ctx_tensors(gc.ctx.ptr, backend);
    if (!gc.buffer.ptr) {
        throw std::runtime_error("ggml_backend_alloc_ctx_tensors failed");
    }
    return gc;
}

static void fill_inputs(const bench_params & params, std::vector<ggml_fp16_t> & weights, std::vector<ggml_fp16_t> & input) {
    weights.resize((size_t) params.in_features * params.out_features);
    input.resize((size_t) params.in_features);
    uint64_t rng = params.seed;
    std::vector<float> row(params.in_features);
    for (int o = 0; o < params.out_features; ++o) {
        for (int i = 0; i < params.in_features; ++i) {
            row[i] = next_uniform(rng);
        }
        ggml_fp32_to_fp16_row(row.data(), weights.data() + (size_t) o * params.in_features, params.in_features);
    }
    for (int i = 0; i < params.in_features; ++i) {
        row[i] = next_uniform(rng);
    }
    ggml_fp32_to_fp16_row(row.data(), input.data(), params.in_features);
}

static std::vector<float> reference_mm(const bench_params & params, const std::vector<ggml_fp16_t> & weights, const std::vector<ggml_fp16_t> & input) {
    std::vector<float> output(params.out_features, 0.0f);
    std::vector<float> input_f32(params.in_features);
    ggml_fp16_to_fp32_row(input.data(), input_f32.data(), params.in_features);
    for (int o = 0; o < params.out_features; ++o) {
        float sum = 0.0f;
        for (int i = 0; i < params.in_features; ++i) {
            sum += ggml_fp16_to_fp32(weights[(size_t) o * params.in_features + i]) * input_f32[i];
        }
        output[o] = sum;
    }
    return output;
}

struct error_metrics {
    float max_abs = 0.0f;
    float max_rel = 0.0f;
    double rmse = 0.0;
    double cosine = 1.0;
    bool passed = false;
};

static error_metrics compare_output(const std::vector<float> & got, const std::vector<float> & ref, const bench_params & params) {
    if (got.size() != ref.size()) {
        throw std::runtime_error("output size mismatch");
    }
    error_metrics e;
    double sq = 0.0;
    double dot = 0.0;
    double got_norm = 0.0;
    double ref_norm = 0.0;
    for (size_t i = 0; i < got.size(); ++i) {
        const float diff = std::fabs(got[i] - ref[i]);
        const float rel = diff / std::max(std::fabs(ref[i]), 1e-6f);
        e.max_abs = std::max(e.max_abs, diff);
        e.max_rel = std::max(e.max_rel, rel);
        sq += (double) diff * diff;
        dot += (double) got[i] * ref[i];
        got_norm += (double) got[i] * got[i];
        ref_norm += (double) ref[i] * ref[i];
    }
    e.rmse = std::sqrt(sq / std::max<size_t>(got.size(), 1));
    e.cosine = dot / std::sqrt(std::max(got_norm * ref_norm, 1e-30));
    e.passed = e.max_abs <= params.abs_tol && e.max_rel <= params.rel_tol;
    return e;
}

static std::vector<std::string> unsupported_compute_nodes(ggml_backend_t backend, ggml_cgraph * graph) {
    std::vector<std::string> unsupported;
    const int n = ggml_graph_n_nodes(graph);
    for (int i = 0; i < n; ++i) {
        ggml_tensor * node = ggml_graph_node(graph, i);
        if (node->op == GGML_OP_NONE) {
            continue;
        }
        if (!ggml_backend_supports_op(backend, node)) {
            unsupported.push_back(ggml_op_desc(node));
        }
    }
    return unsupported;
}

static int compute_nodes(ggml_cgraph * graph) {
    int n_compute = 0;
    for (int i = 0; i < ggml_graph_n_nodes(graph); ++i) {
        if (ggml_graph_node(graph, i)->op != GGML_OP_NONE) {
            ++n_compute;
        }
    }
    return n_compute;
}

static std::string run_mm_decode_f16(const bench_params & params, int & exit_code) {
    ggml_backend_load_all();

    ggml_backend_dev_t dev = find_device(params.backend);
    if (!dev) {
        exit_code = params.fail_on_skipped ? 2 : 0;
        return status_json("skipped", "SKIPPED: requested backend is not registered", params);
    }

    backend_handle backend;
    try {
        backend.ptr = ggml_backend_dev_init(dev, nullptr);
    } catch (const std::exception & exc) {
        exit_code = params.fail_on_skipped ? 2 : 0;
        return status_json("skipped", std::string("SKIPPED: backend init failed: ") + exc.what(), params);
    }
    if (!backend.ptr) {
        exit_code = params.fail_on_skipped ? 2 : 0;
        return status_json("skipped", "SKIPPED: backend init returned null", params);
    }

    std::vector<ggml_fp16_t> weights;
    std::vector<ggml_fp16_t> input;
    fill_inputs(params, weights, input);
    const std::vector<float> ref = reference_mm(params, weights, input);

    graph_case gc = build_mm_case(backend.ptr, params);
    ggml_backend_tensor_set(gc.weights, weights.data(), 0, weights.size() * sizeof(ggml_fp16_t));

    const std::vector<std::string> unsupported = unsupported_compute_nodes(backend.ptr, gc.graph);
    const int n_compute = compute_nodes(gc.graph);
    if (!unsupported.empty()) {
        exit_code = params.fail_on_fallback ? 2 : 0;
        std::ostringstream reason;
        reason << "INVALID: backend does not support graph op";
        for (const auto & op : unsupported) {
            reason << " " << op;
        }
        return status_json("invalid", reason.str(), params);
    }

    std::vector<float> output(params.out_features, 0.0f);
    auto run_once = [&]() -> enum ggml_status {
        ggml_backend_tensor_set(gc.input, input.data(), 0, input.size() * sizeof(ggml_fp16_t));
        enum ggml_status status = ggml_backend_graph_compute(backend.ptr, gc.graph);
        ggml_backend_synchronize(backend.ptr);
        ggml_backend_tensor_get(gc.output, output.data(), 0, output.size() * sizeof(float));
        return status;
    };

    for (int i = 0; i < params.warmup; ++i) {
        enum ggml_status status = run_once();
        if (status != GGML_STATUS_SUCCESS) {
            exit_code = 2;
            return status_json("invalid", std::string("INVALID: warmup graph compute failed: ") + ggml_status_to_string(status), params);
        }
    }

    std::vector<double> times;
    times.reserve(params.iterations);
    for (int i = 0; i < params.iterations; ++i) {
        const auto start = std::chrono::steady_clock::now();
        enum ggml_status status = run_once();
        const auto end = std::chrono::steady_clock::now();
        if (status != GGML_STATUS_SUCCESS) {
            exit_code = 2;
            return status_json("invalid", std::string("INVALID: graph compute failed: ") + ggml_status_to_string(status), params);
        }
        times.push_back(std::chrono::duration<double, std::milli>(end - start).count());
    }

    const error_metrics err = compare_output(output, ref, params);
    const double mean = std::accumulate(times.begin(), times.end(), 0.0) / std::max<size_t>(times.size(), 1);
    const double min_v = *std::min_element(times.begin(), times.end());
    const double max_v = *std::max_element(times.begin(), times.end());
    const double median = percentile(times, 0.50);
    const double p95 = percentile(times, 0.95);
    const uint64_t input_hash = fnv1a64(weights.data(), weights.size() * sizeof(ggml_fp16_t),
        fnv1a64(input.data(), input.size() * sizeof(ggml_fp16_t)));
    const uint64_t output_hash = fnv1a64(output.data(), output.size() * sizeof(float));
    const uint64_t ref_hash = fnv1a64(ref.data(), ref.size() * sizeof(float));

    exit_code = err.passed ? 0 : 2;

    std::ostringstream out;
    out.setf(std::ios::fixed);
    out.precision(6);
    out << "{\n"
        << "  \"schema_version\": 1,\n"
        << "  \"case\": \"mm_decode_f16\",\n"
        << "  \"backend\": \"" << json_escape(params.backend) << "\",\n"
        << "  \"backend_device\": \"" << json_escape(ggml_backend_dev_name(dev)) << "\",\n"
        << "  \"backend_description\": \"" << json_escape(ggml_backend_dev_description(dev)) << "\",\n"
        << "  \"status\": \"" << (err.passed ? "ok" : "invalid") << "\",\n"
        << "  \"shape\": {\"batch\": 1, \"seq\": 1, \"in_features\": " << params.in_features
        << ", \"out_features\": " << params.out_features << "},\n"
        << "  \"precision\": \"f16_weight_f16_activation_f32_output\",\n"
        << "  \"boundary\": \"input_update+graph_compute+synchronize+output_readback\",\n"
        << "  \"warmup\": " << params.warmup << ",\n"
        << "  \"iterations\": " << params.iterations << ",\n"
        << "  \"seed\": " << params.seed << ",\n"
        << "  \"latency_ms\": {\"mean\": " << mean << ", \"median\": " << median
        << ", \"p95\": " << p95 << ", \"min\": " << min_v << ", \"max\": " << max_v << ", \"raw\": [";
    for (size_t i = 0; i < times.size(); ++i) {
        if (i) {
            out << ", ";
        }
        out << times[i];
    }
    out << "]},\n"
        << "  \"correctness\": {\"input_hash_fnv1a64\": \"" << hex_u64(input_hash)
        << "\", \"reference_hash_fnv1a64\": \"" << hex_u64(ref_hash)
        << "\", \"output_hash_fnv1a64\": \"" << hex_u64(output_hash)
        << "\", \"max_abs_err\": " << err.max_abs << ", \"max_rel_err\": " << err.max_rel
        << ", \"rmse\": " << err.rmse << ", \"cosine_similarity\": " << err.cosine
        << ", \"abs_tol\": " << params.abs_tol << ", \"rel_tol\": " << params.rel_tol
        << ", \"passed\": " << (err.passed ? "true" : "false") << "},\n"
        << "  \"backend_assignment\": {\"graph_nodes_total\": " << ggml_graph_n_nodes(gc.graph)
        << ", \"graph_compute_nodes\": " << n_compute
        << ", \"target_backend_nodes\": " << n_compute
        << ", \"cpu_fallback_nodes\": 0, \"unsupported_nodes\": [], \"fallback_count\": 0}\n"
        << "}\n";
    return out.str();
}

int main(int argc, char ** argv) {
    bench_params params;
    try {
        params = parse_args(argc, argv);
    } catch (const std::exception & exc) {
        std::fprintf(stderr, "%s\n", exc.what());
        usage(argv[0]);
        return 1;
    }

    if (params.list_backends) {
        list_backends(std::cout);
        return 0;
    }

    if (params.case_name != "mm_decode_f16") {
        const std::string json = status_json("skipped", "SKIPPED: only mm_decode_f16 is implemented in this benchmark binary", params);
        std::cout << json;
        return params.fail_on_skipped ? 2 : 0;
    }

    int exit_code = 0;
    std::string json;
    try {
        json = run_mm_decode_f16(params, exit_code);
    } catch (const std::exception & exc) {
        json = status_json("invalid", std::string("INVALID: ") + exc.what(), params);
        exit_code = 2;
    }

    if (!params.json_path.empty()) {
        std::ofstream file(params.json_path);
        if (!file) {
            std::fprintf(stderr, "failed to open JSON output: %s\n", params.json_path.c_str());
            return 1;
        }
        file << json;
    }
    std::cout << json;
    return exit_code;
}
