Hexagon HTP unit tests for llama.cpp (Android)
  Context

- Date: Tuesday, December 9, 2025
- OS: Linux
- Repo root: /media/code/llm/llama/llama.cpp (git repo)
- Project: llama.cpp with ggml backends (CPU, OpenCL, Hexagon)
- Build dir: build-snapdragon
- Build target used: ggml-hexagon
- Hexagon SDK: /opt/qcom/Hexagon_SDK/6.4.0.2
- Android NDK used in builds: r28c (arm64-v8a toolchain)
  Key Directories and Files
- Hexagon backend CMake: ggml/src/ggml-hexagon/CMakeLists.txt
- HTP interface IDL: ggml/src/ggml-hexagon/htp/htp_iface.idl
- Host-side HTP unit tests: ggml/src/ggml-hexagon/htp/tests/CMakeLists.txt, test-inverse.cpp
- Generated QAIC artifacts (host): build-snapdragon/ggml/src/ggml-hexagon/htp_iface.h, htp_iface_stub.c
- Host outputs (bin): build-snapdragon/bin/libggml-hexagon.so, libggml-base.so, test-htp-inverse, etc.
- DSP skel libraries (produced via ExternalProject_Add): libggml-htp-v68.so, v69, v73, v75, v79, v81 (installed to the build’s lib install dir; ensure availability for the device’s DSP version)
  What Was Broken
- CMake NOTFOUND variables for ATOMIC_LIB, CDSPRPC_LIB, RPCMEM_LIB when building host unit tests on unsupported/incorrect HLOS configurations.
- QAIC IDL parse error due to invalid “inbuf/outbuf” usage in htp_iface.idl.
  Resolutions Applied

1) CMake/HLOS and Prebuilt Library Paths

- In htp/tests/CMakeLists.txt, set PREBUILT_LIB_DIR based on CMAKE_SYSTEM_NAME:
  - Android arm64-v8a → PREBUILT_LIB_DIR=android_aarch64
  - Android armeabi-v7a → PREBUILT_LIB_DIR=android
  - UbuntuARM arm64 → PREBUILT_LIB_DIR=UbuntuARM_aarch64
- Use include(${HEXAGON_SDK_ROOT}/build/cmake/hexagon_fun.cmake) after setting PREBUILT_LIB_DIR so hexagon_fun can locate prebuilts (cdsprpc/rpcmem/atomic).
- Ensure target_link_libraries signature consistency:
  - Do not mix keyword signature (PRIVATE/PUBLIC/INTERFACE) with plain signature on the same target.
  - Align with hexagon_fun’s behavior (plain signature) for calls that wrap/link host libs, and keep your own target_link_libraries consistent.

2) QAIC IDL Fix

- Replace invalid buffer arguments with QAIC sequences:
  - Old: inbuf src, in uint32 src_len; outbuf dst, in uint32 dst_len
  - New: in sequence`<uint8>` src, rout sequence`<uint8>` dst
- htp_iface.idl now:
  - interface htp_iface : remote_handle64 {
    AEEResult test_unary(in uint32 op_id, in uint32 num_elems,
    in sequence`<uint8>` src, rout sequence`<uint8>` dst);
    }
- This allows qaic -mdll to generate valid htp_iface_stub.c and htp_iface.h.
  Build Commands (used and expected)
- cmake --build build-snapdragon --target ggml-hexagon --verbose
- cmake --build build-snapdragon --target test-htp-inverse --verbose
- ctest -R htp-inverse -C Release --test-dir build-snapdragon (optional)
  Linking Observed
- libggml-hexagon.so links against:
  - /opt/qcom/Hexagon_SDK/6.4.0.2/ipc/fastrpc/remote/ship/android_aarch64/libcdsprpc.so
  - /opt/qcom/Hexagon_SDK/6.4.0.2/ipc/fastrpc/rpcmem/prebuilt/android_aarch64/rpcmem.a
  - -latomic, -llog, -ldl (Android)
- test-htp-inverse links similarly, plus the generated htp_iface stub object.
  Android Deployment and Run Instructions (arm64-v8a)
- Prerequisites on device:
  - FastRPC driver (e.g., /dev/adsprpc-smd exists)
  - Android 64-bit userland for arm64-v8a

1) Push host executable and libs:

- adb push build-snapdragon/bin/test-htp-inverse /data/local/tmp/
- adb push build-snapdragon/bin/libggml-hexagon.so /data/local/tmp/
- adb push build-snapdragon/bin/libggml-base.so /data/local/tmp/
- If needed:
  - adb push /opt/qcom/Hexagon_SDK/6.4.0.2/ipc/fastrpc/remote/ship/android_aarch64/libcdsprpc.so /data/local/tmp/

2) Push DSP skel(s) matching device DSP version:

- e.g., adb push libggml-htp-v79.so /data/local/tmp/
- The correct DSP version (v68/69/73/75/79/81) depends on the target; ensure the skel matches the device’s DSP.

3) Run on device:

- adb shell
- cd /data/local/tmp
- export LD_LIBRARY_PATH=/data/local/tmp:$LD_LIBRARY_PATH
- chmod +x ./test-htp-inverse
- ./test-htp-inverse
- Expected output: “htp inverse unit test passed for N elements”
- If open() fails or skel not found:
  - Verify /dev/adsprpc-smd exists (FastRPC driver)
  - Confirm skel filename/version matches DSP
  - Ensure ABI matches (arm64-v8a)
  - Check LD_LIBRARY_PATH includes directory with libs/skels
    Important Constraints and Practices (apply rigorously)
- Follow project conventions:
  - Use existing CMake patterns (hexagon_fun.cmake, ExternalProject_Add).
  - Keep target_link_libraries signatures consistent across calls.
- Verify libraries/frameworks before use:
  - Hexagon SDK paths: cdsprpc, rpcmem, atomic are resolved via PREBUILT_LIB_DIR and CMAKE_SYSTEM_NAME.
  - Do not assume availability outside the configured HLOS (Android/UbuntuARM).
- Minimal, high-value comments only (explain why, not what).
- Use absolute paths for all file operations.
- Avoid introducing secrets/sensitive data.
- Tests:
  - Add/maintain unit tests under ggml/src/ggml-hexagon/htp/tests; register via CTest.
- Ambiguity:
  - Confirm HLOS/ABI/DSP version before linking or pushing skels.
- Git workflow (if committing):
  - Inspect with: git status && git diff HEAD && git log -n 3
  - Propose clear commit messages focusing on why.
    Common Pitfalls and Notes
- NOTFOUND libs occur if CMAKE_SYSTEM_NAME is not one of hexagon_fun-supported HLOS (“Android”, “UbuntuARM”, etc.), or PREBUILT_LIB_DIR is unset/mismatched.
- QAIC IDL must use supported syntax (sequence `<T>`, rout, etc.); “inbuf/outbuf” is invalid in this SDK version.
- Mixing keyword and plain signatures in target_link_libraries on the same target triggers CMake errors. Keep a single consistent style.
- Ensure the DSP skel library version matches the device; otherwise FastRPC open fails.
- On Android, -latomic may be required; toolchain typically provides it.
  Summary
- The host unit test test-htp-inverse and libggml-hexagon.so are built successfully against Hexagon SDK (Android arm64-aarch64), after:
  - Setting PREBUILT_LIB_DIR per HLOS/ABI
  - Fixing QAIC IDL buffer parameters to sequences
  - Harmonizing target_link_libraries signatures
- To run on Android, push host binary/libs and matching DSP skel(s), set LD_LIBRARY_PATH, and execute test-htp-inverse on the device with FastRPC available.
