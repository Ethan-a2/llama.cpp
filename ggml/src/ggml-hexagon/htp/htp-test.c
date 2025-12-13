#include <AEEStdErr.h>
#include <HAP_farf.h>
#include <hexagon_types.h>
#include <hexagon_protos.h>
#include <string.h>

#ifdef HTP_DEBUG
#    define FARF_HIGH 1
#endif

#define GGML_COMMON_DECL_C
#include "ggml-common.h"
#include "hvx-utils.h"

// IDL-implemented symbol naming: __QAIC_IMPL(htp_iface_test_unary)
AEEResult htp_iface_test_unary(remote_handle64 handle,
                               uint32 op_id,
                               uint32 num_elems,
                               const unsigned char * src, uint32 src_len,
                               unsigned char * dst, uint32 dst_len) {
    (void) handle;

    if (src == NULL || dst == NULL) {
        return AEE_EBADPARM;
    }

    const uint32 needed = num_elems * sizeof(float);
    if (src_len < needed || dst_len < needed) {
        FARF(ERROR, "test_unary: buffer too small src_len=%u dst_len=%u needed=%u",
             (unsigned) src_len, (unsigned) dst_len, (unsigned) needed);
        return AEE_EBADPARM;
    }

    switch (op_id) {
        case 0: // inverse fp32
            hvx_inverse_f32((const uint8_t *) src, (uint8_t *) dst, (int) num_elems);
            break;
        default:
            FARF(ERROR, "test_unary: unsupported op_id %u", (unsigned) op_id);
            return AEE_EUNSUPPORTED;
    }

    return AEE_SUCCESS;
}
