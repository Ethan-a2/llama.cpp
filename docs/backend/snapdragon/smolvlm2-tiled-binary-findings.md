# SmolVLM2 tiled binary investigation

## Current patch status

The nine-line guard in `ggml_hexagon_supported_binary()` is a conservative workaround, not native tiled binary support. It rejects eligible F16/F32 leaf tensors on either input. A leaf tensor is not necessarily a weight, so the guard rejects some valid flat inputs too. It does not cover every view or runtime repack case.

The original ADD suite executed 80 cases; the patched suite executed 74. The following previously passing cases now report unsupported. These are coverage losses, not correctness passes:

```text
ADD(type=f16,ne=[1280,1,1,1],nr=[1,256,1,1],nf=1,perm1=0,src_overlap=0)
ADD(type=f16,ne=[5120,1,1,1],nr=[1,256,1,1],nf=1,perm1=0,src_overlap=0)
ADD(type=f16,ne=[64,262144,1,1],nr=[1,1,1,1],nf=1,perm1=0,src_overlap=0)
ADD(type=f32,ne=[1280,1,1,1],nr=[1,256,1,1],nf=1,perm1=0,src_overlap=0)
ADD(type=f32,ne=[5120,1,1,1],nr=[1,256,1,1],nf=1,perm1=0,src_overlap=0)
ADD(type=f32,ne=[64,262144,1,1],nr=[1,1,1,1],nf=1,perm1=0,src_overlap=0)
```

## mmproj quantization check

The Hugging Face repository contains Q8_0, f16 and BF16 mmproj files. GGUF header inspection shows that the relevant tensor remains F32 in all three files:

```text
mmproj-SmolVLM2-256M-Video-Instruct-Q8_0.gguf   v.position_embd.weight [768,1024] type F32
mmproj-SmolVLM2-256M-Video-Instruct-f16.gguf     v.position_embd.weight [768,1024] type F32
mmproj-SmolVLM2-256M-Video-Instruct-BF16.gguf    v.position_embd.weight [768,1024] type F32
```

The files differ in other projector weights, but the tensor that first diverges is not Q8_0, F16 or BF16. It is F32 in the source GGUF and is then converted by the HTP weight path to F16 tiled storage. Therefore changing only the mmproj quantization does not remove the layout bug: with the original backend, all three variants are expected to hit the same F32 tiled-weight plus binary-ADD mismatch. With the current conservative guard, all three should avoid the bad HTP binary path by falling back for the affected operation. This metadata conclusion should still be followed by an end-to-end run for each variant before publishing a performance comparison.

## Performance evidence

Historical single runs, not controlled A/B benchmarks:

| Metric | Original HTP configuration | Guarded configuration |
| --- | ---: | ---: |
| Vision encoding, 13 batches | 1151 ms | 1309 ms |
| Prompt eval, 878 tokens | 7188.07 ms | 8094.10 ms |
| Decode runs | 31 | 20 |
| Decode rate | 34.52 token/s | 23.40 token/s |
| Context total | 8796.41 ms | 9488.23 ms |

Vision time increased by 158 ms (13.7%). Decode and total are not directly comparable because generated sequences and lengths differ. Layer offload does not prove pure HTP execution or unchanged LLM performance. These runs include existing CPU fallbacks.

## Native implementation direction

The native tiled ADD prototype was withdrawn at the user's request before compilation or deployment. No prototype kernel or protocol changes remain. The retained workaround has the same executable logic as the previously tested guard; only its source comments were expanded.

## Workaround rationale and limits

- `supports_op` is queried before buffers are allocated or populated. Inspecting buffer usage or the runtime tiled flag here would make support depend on allocation order. The guard uses static properties instead.
- `GGML_OP_NONE` identifies leaf tensors, not weights. Combined with F16/F32 type and tiled shape eligibility, it conservatively rejects both unsafe weights and some safe flat inputs.
- Both inputs are checked. Checking only the second input would miss a weight supplied as the first operand.
- The guard changes binary support only. It does not disable matmul tiling, change the DSP protocol, or add a model-name exception. Other supported backends can execute rejected nodes; the device tests used CPU fallback.
- The guard does not prove that all remaining tiled layouts are safe. Views and runtime repacking need separate coverage. It must not be presented as full native tiled binary support.
- BF16 inputs already fail the existing binary type checks. The BF16 mmproj's position tensor is F32, so it is covered by this guard.
- The original 80-case ADD run and the patched 74-case run are not equivalent coverage. A weight-buffer and scheduler regression test is still missing.

Detailed analysis belongs in this document; the source comment remains short. Before committing, review the backend diff and these limitations. Stage only the backend file and the two analysis documents, not `pkg-snapdragon/`, model weights, personal presets, or the image with unverified redistribution terms. The native prototype was never installed, so the device remains on the previously tested workaround build.

## Deferred native implementation

Preserve packed matmul weights and add a DSP binary dispatch for flat F32 plus tiled F16 storage. Keep ordinary flat kernels unchanged. Reuse `hvx_dequantize_row_tiled_f16_f32()` but load each 32-row tile group once and reuse it across its rows; copying a complete tile group for each row wastes bandwidth.

Actual storage flags must select the execution path after allocation. Static support checks must guarantee that every accepted layout has an execution path. Returning a DSP error is not automatic scheduler CPU fallback.

The initial scope should explicitly bound supported shapes, broadcasting, source order and types. Preserve logical output type through the existing graph/kernel parameters where possible; a global tensor protocol change is not yet justified. Add weight-buffer regression tests and flat-input coverage tests before removing the workaround. Test multidevice partitioning, VTCM bounds and synchronization as well as arithmetic.

Native performance cannot yet be predicted numerically. The prior 0-5% overhead estimate was a target, not measurement.

## Evidence files on the host

```text
/tmp/opencode/smolvlm2-add-unit.log
/tmp/opencode/smolvlm2-fixed-add-tests.log
/tmp/opencode/smolvlm2-htp-success.log
/tmp/opencode/smolvlm2-fixed-htpllm.log
/tmp/opencode/smolvlm2-embed-cpu.bin
/tmp/opencode/smolvlm2-embed-htp.bin
/tmp/opencode/smolvlm2-fixed.bin
```

Embedding dumps contain only the final image block because each block overwrites the file. Model weights and generated installation packages should not be committed. The existing `cats.jpg` requires a provenance and redistribution check before inclusion.
