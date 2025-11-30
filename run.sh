cmake --build build-snapdragon --target ggml-hexagon --verbose
adb push ./build-snapdragon/bin/libggml-hexagon.so /data/local/tmp/llama.cpp/lib/libggml-hexagon.so
V=0 PROF=1 M=LFM2-350M-Q4_0.gguf D=HTP0 ./scripts/snapdragon/adb/run-cli.sh -no-cnv -p \"1+1=?\" --verbose
# V=1 PROF=1 M=LFM2-350M-Q4_0.gguf D=HTP0 ./scripts/snapdragon/adb/run-cli.sh -no-cnv -p \"1+1=?\" --verbose
