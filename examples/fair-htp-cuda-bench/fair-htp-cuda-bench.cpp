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
        "usage: %s [--backend cpu|cuda|htp|hexagon] [--case CASE] [options]\n"
        "cases: mm_decode_f16, elementwise_fusion_f32, copy_f16_to_f32, kv_set_rows_f16\n"
        "options:\n"
        "  --in N                 input features, width, or vector side (default 1024)\n"
        "  --out N                output features, rows, or vector side (default 1024)\n"
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
    ggml_tensor * a = nullptr;
    ggml_tensor * b = nullptr;
    ggml_tensor * c = nullptr;
    ggml_tensor * dst = nullptr;
    ggml_tensor * cache = nullptr;
    ggml_tensor * update = nullptr;
    ggml_tensor * indices = nullptr;
    ggml_tensor * output = nullptr;
    ggml_cgraph * graph = nullptr;
    buffer_handle buffer;
};

struct tensor_upload {
    ggml_tensor * tensor = nullptr;
    const void * data = nullptr;
    size_t size = 0;
};

struct prepared_case {
    graph_case gc;
    std::vector<ggml_fp16_t> weights_f16;
    std::vector<ggml_fp16_t> input_f16;
    std::vector<ggml_fp16_t> cache_f16;
    std::vector<float> a_f32;
    std::vector<float> b_f32;
    std::vector<float> c_f32;
    std::vector<float> update_f32;
    std::vector<int32_t> indices_i32;
    std::vector<float> ref;
    std::vector<tensor_upload> static_uploads;
    std::vector<tensor_upload> dynamic_uploads;
    std::string shape_json;
    std::string precision;
};

static size_t checked_product(int a, int b) {
    const size_t aa = (size_t) a;
    const size_t bb = (size_t) b;
    if (aa != 0 && bb > SIZE_MAX / aa) {
        throw std::runtime_error("shape is too large");
    }
    return aa * bb;
}

static void init_context(graph_case & gc) {
    const size_t ctx_size = 64ull * 1024ull * 1024ull;
    ggml_init_params init = { ctx_size, nullptr, true };
    gc.ctx.ptr = ggml_init(init);
    if (!gc.ctx.ptr) {
        throw std::runtime_error("ggml_init failed");
    }
}

static void finish_graph(ggml_backend_t backend, graph_case & gc) {
    gc.graph = ggml_new_graph(gc.ctx.ptr);
    ggml_build_forward_expand(gc.graph, gc.output);

    gc.buffer.ptr = ggml_backend_alloc_ctx_tensors(gc.ctx.ptr, backend);
    if (!gc.buffer.ptr) {
        throw std::runtime_error("ggml_backend_alloc_ctx_tensors failed");
    }
}

static void add_upload(std::vector<tensor_upload> & uploads, ggml_tensor * tensor, const void * data, size_t size) {
    uploads.push_back({tensor, data, size});
}

static void upload_all(const std::vector<tensor_upload> & uploads) {
    for (const tensor_upload & upload : uploads) {
        ggml_backend_tensor_set(upload.tensor, upload.data, 0, upload.size);
    }
}

static uint64_t hash_uploads(const std::vector<tensor_upload> & uploads, uint64_t hash = 1469598103934665603ull) {
    for (const tensor_upload & upload : uploads) {
        hash = fnv1a64(upload.data, upload.size, hash);
    }
    return hash;
}

static void read_tensor_as_f32(ggml_tensor * tensor, std::vector<float> & output) {
    const int64_t n = ggml_nelements(tensor);
    if (n < 0) {
        throw std::runtime_error("negative output element count");
    }
    output.resize((size_t) n);
    if (tensor->type == GGML_TYPE_F32) {
        ggml_backend_tensor_get(tensor, output.data(), 0, output.size() * sizeof(float));
        return;
    }
    if (tensor->type == GGML_TYPE_F16) {
        std::vector<ggml_fp16_t> tmp(output.size());
        ggml_backend_tensor_get(tensor, tmp.data(), 0, tmp.size() * sizeof(ggml_fp16_t));
        ggml_fp16_to_fp32_row(tmp.data(), output.data(), (int64_t) output.size());
        return;
    }
    throw std::runtime_error("unsupported output tensor type");
}

static void build_mm_case(ggml_backend_t backend, const bench_params & params, prepared_case & pc) {
    init_context(pc.gc);

    pc.gc.weights = ggml_new_tensor_2d(pc.gc.ctx.ptr, GGML_TYPE_F16, params.in_features, params.out_features);
    pc.gc.input   = ggml_new_tensor_2d(pc.gc.ctx.ptr, GGML_TYPE_F16, params.in_features, 1);
    ggml_set_name(pc.gc.weights, "weights_f16");
    ggml_set_name(pc.gc.input, "input_f16");

    pc.gc.output = ggml_mul_mat(pc.gc.ctx.ptr, pc.gc.weights, pc.gc.input);
    ggml_set_name(pc.gc.output, "output_f32");
    if (pc.gc.output->type != GGML_TYPE_F32) {
        throw std::runtime_error("mm_decode_f16 output is not F32");
    }

    pc.weights_f16.resize((size_t) params.in_features * params.out_features);
    pc.input_f16.resize((size_t) params.in_features);
    uint64_t rng = params.seed;
    std::vector<float> row(params.in_features);
    for (int o = 0; o < params.out_features; ++o) {
        for (int i = 0; i < params.in_features; ++i) {
            row[i] = next_uniform(rng);
        }
        ggml_fp32_to_fp16_row(row.data(), pc.weights_f16.data() + (size_t) o * params.in_features, params.in_features);
    }
    for (int i = 0; i < params.in_features; ++i) {
        row[i] = next_uniform(rng);
    }
    ggml_fp32_to_fp16_row(row.data(), pc.input_f16.data(), params.in_features);

    pc.ref.assign(params.out_features, 0.0f);
    std::vector<float> input_f32(params.in_features);
    ggml_fp16_to_fp32_row(pc.input_f16.data(), input_f32.data(), params.in_features);
    for (int o = 0; o < params.out_features; ++o) {
        float sum = 0.0f;
        for (int i = 0; i < params.in_features; ++i) {
            sum += ggml_fp16_to_fp32(pc.weights_f16[(size_t) o * params.in_features + i]) * input_f32[i];
        }
        pc.ref[o] = sum;
    }

    pc.shape_json = "{\"batch\": 1, \"seq\": 1, \"in_features\": " + std::to_string(params.in_features) +
        ", \"out_features\": " + std::to_string(params.out_features) + "}";
    pc.precision = "f16_weight_f16_activation_f32_output";
    finish_graph(backend, pc.gc);
    add_upload(pc.static_uploads, pc.gc.weights, pc.weights_f16.data(), pc.weights_f16.size() * sizeof(ggml_fp16_t));
    add_upload(pc.dynamic_uploads, pc.gc.input, pc.input_f16.data(), pc.input_f16.size() * sizeof(ggml_fp16_t));
}

static void fill_f32(std::vector<float> & data, uint64_t & rng) {
    for (float & value : data) {
        value = next_uniform(rng);
    }
}

static void build_elementwise_fusion_case(ggml_backend_t backend, const bench_params & params, prepared_case & pc) {
    init_context(pc.gc);
    const size_t n = checked_product(params.in_features, params.out_features);

    pc.gc.a = ggml_new_tensor_1d(pc.gc.ctx.ptr, GGML_TYPE_F32, (int64_t) n);
    pc.gc.b = ggml_new_tensor_1d(pc.gc.ctx.ptr, GGML_TYPE_F32, (int64_t) n);
    pc.gc.c = ggml_new_tensor_1d(pc.gc.ctx.ptr, GGML_TYPE_F32, (int64_t) n);
    ggml_set_name(pc.gc.a, "a_f32");
    ggml_set_name(pc.gc.b, "b_f32");
    ggml_set_name(pc.gc.c, "c_f32");
    pc.gc.output = ggml_add(pc.gc.ctx.ptr, ggml_mul(pc.gc.ctx.ptr, pc.gc.a, pc.gc.b), pc.gc.c);
    ggml_set_name(pc.gc.output, "fused_output_f32");

    pc.a_f32.resize(n);
    pc.b_f32.resize(n);
    pc.c_f32.resize(n);
    pc.ref.resize(n);
    uint64_t rng = params.seed;
    fill_f32(pc.a_f32, rng);
    fill_f32(pc.b_f32, rng);
    fill_f32(pc.c_f32, rng);
    for (size_t i = 0; i < n; ++i) {
        pc.ref[i] = pc.a_f32[i] * pc.b_f32[i] + pc.c_f32[i];
    }

    pc.shape_json = "{\"elements\": " + std::to_string(n) +
        ", \"logical_shape\": [" + std::to_string(params.in_features) + ", " + std::to_string(params.out_features) + "]}";
    pc.precision = "f32_inputs_f32_output";
    finish_graph(backend, pc.gc);
    add_upload(pc.dynamic_uploads, pc.gc.a, pc.a_f32.data(), pc.a_f32.size() * sizeof(float));
    add_upload(pc.dynamic_uploads, pc.gc.b, pc.b_f32.data(), pc.b_f32.size() * sizeof(float));
    add_upload(pc.dynamic_uploads, pc.gc.c, pc.c_f32.data(), pc.c_f32.size() * sizeof(float));
}

static void build_copy_case(ggml_backend_t backend, const bench_params & params, prepared_case & pc) {
    init_context(pc.gc);
    const size_t n = checked_product(params.in_features, params.out_features);

    pc.gc.input = ggml_new_tensor_1d(pc.gc.ctx.ptr, GGML_TYPE_F16, (int64_t) n);
    pc.gc.dst = ggml_new_tensor_1d(pc.gc.ctx.ptr, GGML_TYPE_F32, (int64_t) n);
    ggml_set_name(pc.gc.input, "copy_input_f16");
    ggml_set_name(pc.gc.dst, "copy_dst_f32");
    pc.gc.output = ggml_cpy(pc.gc.ctx.ptr, pc.gc.input, pc.gc.dst);
    ggml_set_name(pc.gc.output, "copy_output_f32");

    pc.input_f16.resize(n);
    pc.ref.resize(n);
    std::vector<float> tmp(n);
    uint64_t rng = params.seed;
    fill_f32(tmp, rng);
    ggml_fp32_to_fp16_row(tmp.data(), pc.input_f16.data(), (int64_t) n);
    ggml_fp16_to_fp32_row(pc.input_f16.data(), pc.ref.data(), (int64_t) n);

    pc.shape_json = "{\"elements\": " + std::to_string(n) +
        ", \"logical_shape\": [" + std::to_string(params.in_features) + ", " + std::to_string(params.out_features) + "]}";
    pc.precision = "f16_input_f32_output";
    finish_graph(backend, pc.gc);
    add_upload(pc.dynamic_uploads, pc.gc.input, pc.input_f16.data(), pc.input_f16.size() * sizeof(ggml_fp16_t));
}

static void build_kv_set_rows_case(ggml_backend_t backend, const bench_params & params, prepared_case & pc) {
    init_context(pc.gc);
    const int width = params.in_features;
    const int update_rows = params.out_features;
    const int cache_rows = update_rows * 2 + 7;
    const size_t cache_elements = checked_product(width, cache_rows);
    const size_t update_elements = checked_product(width, update_rows);

    pc.gc.cache = ggml_new_tensor_2d(pc.gc.ctx.ptr, GGML_TYPE_F16, width, cache_rows);
    pc.gc.update = ggml_new_tensor_2d(pc.gc.ctx.ptr, GGML_TYPE_F32, width, update_rows);
    pc.gc.indices = ggml_new_tensor_1d(pc.gc.ctx.ptr, GGML_TYPE_I32, update_rows);
    ggml_set_name(pc.gc.cache, "kv_cache_f16");
    ggml_set_name(pc.gc.update, "kv_update_f32");
    ggml_set_name(pc.gc.indices, "kv_indices_i32");
    pc.gc.output = ggml_set_rows(pc.gc.ctx.ptr, pc.gc.cache, pc.gc.update, pc.gc.indices);
    ggml_set_name(pc.gc.output, "kv_cache_updated_f16");

    pc.cache_f16.resize(cache_elements);
    pc.update_f32.resize(update_elements);
    pc.indices_i32.resize(update_rows);
    pc.ref.resize(cache_elements);
    std::vector<float> tmp(cache_elements);
    uint64_t rng = params.seed;
    fill_f32(tmp, rng);
    ggml_fp32_to_fp16_row(tmp.data(), pc.cache_f16.data(), (int64_t) cache_elements);
    ggml_fp16_to_fp32_row(pc.cache_f16.data(), pc.ref.data(), (int64_t) cache_elements);
    fill_f32(pc.update_f32, rng);
    for (int row = 0; row < update_rows; ++row) {
        pc.indices_i32[row] = (row * 2 + 3) % cache_rows;
        const int dst_row = pc.indices_i32[row];
        for (int col = 0; col < width; ++col) {
            const size_t src_index = (size_t) row * width + col;
            const size_t dst_index = (size_t) dst_row * width + col;
            pc.ref[dst_index] = ggml_fp16_to_fp32(ggml_fp32_to_fp16(pc.update_f32[src_index]));
        }
    }

    pc.shape_json = "{\"width\": " + std::to_string(width) +
        ", \"update_rows\": " + std::to_string(update_rows) +
        ", \"cache_rows\": " + std::to_string(cache_rows) + "}";
    pc.precision = "f16_cache_f32_update_f16_output";
    finish_graph(backend, pc.gc);
    add_upload(pc.dynamic_uploads, pc.gc.cache, pc.cache_f16.data(), pc.cache_f16.size() * sizeof(ggml_fp16_t));
    add_upload(pc.dynamic_uploads, pc.gc.update, pc.update_f32.data(), pc.update_f32.size() * sizeof(float));
    add_upload(pc.dynamic_uploads, pc.gc.indices, pc.indices_i32.data(), pc.indices_i32.size() * sizeof(int32_t));
}

static bool build_selected_case(ggml_backend_t backend, const bench_params & params, prepared_case & pc) {
    if (params.case_name == "mm_decode_f16") {
        build_mm_case(backend, params, pc);
        return true;
    }
    if (params.case_name == "elementwise_fusion_f32") {
        build_elementwise_fusion_case(backend, params, pc);
        return true;
    }
    if (params.case_name == "copy_f16_to_f32") {
        build_copy_case(backend, params, pc);
        return true;
    }
    if (params.case_name == "kv_set_rows_f16") {
        build_kv_set_rows_case(backend, params, pc);
        return true;
    }
    return false;
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

static std::string success_json(
        const bench_params & params,
        ggml_backend_dev_t dev,
        const prepared_case & pc,
        const std::vector<double> & times,
        const std::vector<float> & output,
        const error_metrics & err,
        int n_compute) {
    const double mean = std::accumulate(times.begin(), times.end(), 0.0) / std::max<size_t>(times.size(), 1);
    const double min_v = *std::min_element(times.begin(), times.end());
    const double max_v = *std::max_element(times.begin(), times.end());
    const double median = percentile(times, 0.50);
    const double p95 = percentile(times, 0.95);
    const uint64_t input_hash = hash_uploads(pc.dynamic_uploads, hash_uploads(pc.static_uploads));
    const uint64_t output_hash = fnv1a64(output.data(), output.size() * sizeof(float));
    const uint64_t ref_hash = fnv1a64(pc.ref.data(), pc.ref.size() * sizeof(float));

    std::ostringstream out;
    out.setf(std::ios::fixed);
    out.precision(6);
    out << "{\n"
        << "  \"schema_version\": 1,\n"
        << "  \"case\": \"" << json_escape(params.case_name) << "\",\n"
        << "  \"backend\": \"" << json_escape(params.backend) << "\",\n"
        << "  \"backend_device\": \"" << json_escape(ggml_backend_dev_name(dev)) << "\",\n"
        << "  \"backend_description\": \"" << json_escape(ggml_backend_dev_description(dev)) << "\",\n"
        << "  \"status\": \"" << (err.passed ? "ok" : "invalid") << "\",\n"
        << "  \"shape\": " << pc.shape_json << ",\n"
        << "  \"precision\": \"" << json_escape(pc.precision) << "\",\n"
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
        << "  \"backend_assignment\": {\"graph_nodes_total\": " << ggml_graph_n_nodes(pc.gc.graph)
        << ", \"graph_compute_nodes\": " << n_compute
        << ", \"target_backend_nodes\": " << n_compute
        << ", \"cpu_fallback_nodes\": 0, \"unsupported_nodes\": [], \"fallback_count\": 0}\n"
        << "}\n";
    return out.str();
}

static std::string run_fair_case(const bench_params & params, int & exit_code) {
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

    prepared_case pc;
    if (!build_selected_case(backend.ptr, params, pc)) {
        exit_code = params.fail_on_skipped ? 2 : 0;
        return status_json("skipped", "SKIPPED: requested case is not implemented", params);
    }
    upload_all(pc.static_uploads);

    const std::vector<std::string> unsupported = unsupported_compute_nodes(backend.ptr, pc.gc.graph);
    const int n_compute = compute_nodes(pc.gc.graph);
    if (!unsupported.empty()) {
        exit_code = params.fail_on_fallback ? 2 : 0;
        std::ostringstream reason;
        reason << "INVALID: backend does not support graph op";
        for (const auto & op : unsupported) {
            reason << " " << op;
        }
        return status_json("invalid", reason.str(), params);
    }

    std::vector<float> output;
    auto run_once = [&]() -> enum ggml_status {
        upload_all(pc.dynamic_uploads);
        enum ggml_status status = ggml_backend_graph_compute(backend.ptr, pc.gc.graph);
        ggml_backend_synchronize(backend.ptr);
        if (status == GGML_STATUS_SUCCESS) {
            read_tensor_as_f32(pc.gc.output, output);
        }
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

    const error_metrics err = compare_output(output, pc.ref, params);
    exit_code = err.passed ? 0 : 2;
    return success_json(params, dev, pc, times, output, err, n_compute);
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

    int exit_code = 0;
    std::string json;
    try {
        json = run_fair_case(params, exit_code);
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
