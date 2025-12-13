#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>
#include <limits>
#include <cstring>
#include <stdexcept>
#include <string>

extern "C" {
#include "htp_iface.h"
}

// // Fallback/defaults if your environment doesn't provide above symbols:
// #ifndef CDSP_DOMAIN_NAME
// #define CDSP_DOMAIN_NAME "cdsp"
// #endif
// #ifndef MAX_DOMAIN_NAMELEN
// #define MAX_DOMAIN_NAMELEN 32
// #endif

static void fill_inputs(std::vector<float>& v, bool with_edges) {
    for (size_t i = 0; i < v.size(); ++i) {
        float x = (float)((i % 97) - 48) / 13.0f; // mixed positives/negatives
        if (with_edges) {
            if (i % 31 == 0) x = 0.0f;                 // zeros
            if (i % 127 == 0) x = INFINITY;           // +inf
            if (i % 257 == 0) x = -INFINITY;          // -inf
            if (i % 521 == 0) x = std::numeric_limits<float>::quiet_NaN(); // NaN
        }
        v[i] = x;
    }
}

/*
 Helper to open htp session similar to ggml_hexagon_session::allocate().
 Tries to use remote_session_control to create/get session URI and enable unsigned PD when available.
 On success returns AEE_SUCCESS and fills *out_h with opened handle.
 On failure returns last error code from htp_iface_open or remote_session_control.
*/
static int open_htp_session(int dev_id, remote_handle64 *out_h) {
    if (!out_h) return -1;

    // Use opt_arch consistent with the example lib name. Adjust if needed.
    unsigned opt_arch = 79;

    // Build module URI (module file) which can be appended with domain info for single-session fallback.
    char module_uri[256];
    snprintf(module_uri, sizeof(module_uri),
             "file:///libggml-htp-v%u.so?htp_iface_skel_handle_invoke&_modver=1.0", opt_arch);

    // Default session_uri is module_uri (fallback)
    char session_uri[512];
    session_uri[0] = '\0';

    // If fastRPC is available, attempt to reserve session and query URI.
    // Note: struct definitions for remote_rpc_reserve_new_session and remote_rpc_get_uri are project-specific.
    // The code below mirrors ggml_hexagon_session::allocate() and must be linked against fastRPC library.
    struct remote_rpc_reserve_new_session n = {};
    {
        // Reserve new session (only if dev_id != 0 in ggml code; here honor same behavior)
        if (dev_id != 0) {
            n.domain_name_len  = strlen(CDSP_DOMAIN_NAME);
            n.domain_name      = const_cast<char *>(CDSP_DOMAIN_NAME);
            // session_name - give a simple name
            std::string sess_name = std::string("HTP") + std::to_string(dev_id);
            n.session_name     = const_cast<char *>(sess_name.c_str());
            n.session_name_len = (int)sess_name.size();

            int err = remote_session_control(FASTRPC_RESERVE_NEW_SESSION, (void*)&n, sizeof(n));
            if (err != AEE_SUCCESS) {
                std::fprintf(stderr, "remote_session_control(RESERVE_NEW_SESSION) failed: 0x%08x\n", err);
                return err;
            }
            // optional: you can use n.session_id / n.effective_domain_id if needed
        }

        // Try to GET_URI for the session (this populates session_uri)
        struct remote_rpc_get_uri u = {};
        u.session_id      = n.session_id; // if you reserved, put the reserved session id instead
        u.domain_name     = const_cast<char *>(CDSP_DOMAIN_NAME);
        u.domain_name_len = strlen(CDSP_DOMAIN_NAME);
        u.module_uri      = const_cast<char *>(module_uri);
        u.module_uri_len  = strlen(module_uri);
        u.uri             = session_uri;
        u.uri_len         = (int)sizeof(session_uri);

        int err_geturi = remote_session_control(FASTRPC_GET_URI, (void*)&u, sizeof(u));
        if (err_geturi != AEE_SUCCESS) {
            // Fallback: attempt to build single-session URI by concatenating module_uri + domain->uri
            // If domain info available in your platform, append it here. Otherwise fallback to module_uri.
            // The original allocate() used my_domain->uri when FASTRPC_GET_URI failed.
            std::snprintf(session_uri, sizeof(session_uri), "%s&_dom=%s", module_uri, CDSP_DOMAIN_NAME);
            std::fprintf(stderr, "remote_session_control(GET_URI) failed: 0x%08x. Falling back to module uri: %s\n",
                         err_geturi, session_uri);
            // proceed — we still can attempt open with fallback session_uri
        }

        // Enable unsigned PD for the domain (optional but mirrors allocate())
        struct remote_rpc_control_unsigned_module u2 = {};
        // If you reserved session, you might want to use its effective domain id instead of 0.
        u2.domain = n.effective_domain_id;//CDSP_DOMAIN_ID; // placeholder; set to proper domain id if available
        u2.enable = 1;
        int err_en = remote_session_control(DSPRPC_CONTROL_UNSIGNED_MODULE, (void*)&u2, sizeof(u2));
        if (err_en != AEE_SUCCESS) {
            std::fprintf(stderr, "remote_session_control(DSPRPC_CONTROL_UNSIGNED_MODULE) failed: 0x%08x\n", err_en);
            // Not fatal in some flows — choose to continue or return error. Here we continue with a warning.
        }
    }

    // Final: open session via htp_iface_open
    int err = htp_iface_open(session_uri, out_h);
    if (err != AEE_SUCCESS) {
        std::fprintf(stderr, "htp_iface_open(%s) failed: 0x%08x\n", session_uri, err);
        return err;
    }

    return AEE_SUCCESS;
}

int main() {
    const uint32_t n = 4096 + 37; // cover whole + leftover

    std::vector<float> src(n);
    std::vector<float> dst(n, 0.0f);
    fill_inputs(src, /*with_edges=*/true);

    remote_handle64 h = 0;

    // Try to open a session using helper that mirrors ggml_hexagon_session::allocate()
    int err = open_htp_session(/*dev_id=*/0, &h);
    if (err != AEE_SUCCESS) {
        std::fprintf(stderr, "open_htp_session failed: 0x%08x\n", err);
        return 1;
    }

    // Call DSP unary test for inverse (op_id=0)
    err = htp_iface_test_unary(h, /*op_id=*/0, n,
                               reinterpret_cast<const unsigned char*>(src.data()), src.size() * sizeof(float),
                               reinterpret_cast<unsigned char*>(dst.data()), dst.size() * sizeof(float));
    if (err != AEE_SUCCESS) {
        std::fprintf(stderr, "htp_iface_test_unary failed: 0x%08x\n", err);
        htp_iface_close(h);
        return 2;
    }

    // Verify results against CPU reference with dsp guard semantics
    const float rel_tol = 1e-5f;
    const float abs_tol = 1e-6f;
    for (uint32_t i = 0; i < n; ++i) {
        float x = src[i];
        float y = dst[i];
        if (std::isnan(x) || std::isinf(x) || x == 0.0f) {
            if (!(y == 0.0f)) {
                std::fprintf(stderr, "mismatch at %u: edge x=%g, got y=%g, expect 0\n", i, x, y);
                htp_iface_close(h);
                return 3;
            }
        } else {
            float ref = 1.0f / x;
            float diff = std::fabs(ref - y);
            float thr = std::max(abs_tol, rel_tol * std::fabs(ref));
            if (diff > thr) {
                std::fprintf(stderr, "mismatch at %u: x=%g ref=%g y=%g diff=%g thr=%g\n", i, x, ref, y, diff, thr);
                htp_iface_close(h);
                return 4;
            }
        }
    }

    htp_iface_close(h);
    std::printf("htp inverse unit test passed for %u elements\n", n);
    return 0;
}
