# Android HTP 部署验证：SmolLM2-360M

本文也包含 SmolVLM2-256M 多模态测试记录，见第 12 节及后续章节。SmolLM2 的纯文本结果与 SmolVLM2 的图片理解结果应分别阅读。

本文记录在 Snapdragon Android 手机上，使用 llama.cpp Snapdragon/Hexagon 后端运行
`SmolLM2-360M-Instruct-Q4_K_M.gguf` 的完整过程。本文中的命令和结果已经在一台
SM8850 手机上实际验证。结论是 HTP 参与执行的 CPU/HTP 混合推理成功，不是全部算子都在 HTP 上执行。文中的性能来自一次短生成测试，不代表纯 HTP 性能。

## 1. 验证环境

### 主机

| 项目 | 值 |
| --- | --- |
| llama.cpp 工作目录 | `/media/code/llm/llama/llama.cpp` |
| Snapdragon 安装包 | `pkg-snapdragon/llama.cpp` |
| 模型主机路径 | `/media/prj/gguf/SmolLM2-360M-Instruct-Q4_K_M.gguf` |
| ADB | 主机可执行 `adb` |
| Docker | 用于 Snapdragon Android 交叉编译 |

### 手机

| 项目 | 实测值 |
| --- | --- |
| ADB serial | `c495c2c3` |
| 产品/型号标识 | `nezha` / `2512BPNDAC` |
| SoC | `SM8850` |
| Android | `16` |
| HTP 架构 | `v81` |

检查手机是否连接：

```bash
adb devices -l
adb shell getprop ro.soc.model
adb shell getprop ro.build.version.release
```

预期可以看到类似结果：

```text
c495c2c3  device  ... model:2512BPNDAC ...
SM8850
16
```

## 2. 构建产物要求

HTP 运行需要 Android ARM64 主程序、主机侧 Hexagon backend，以及对应 HTP DSP
侧的 skel 库。安装包至少应包含：

```text
pkg-snapdragon/llama.cpp/bin/llama-cli
pkg-snapdragon/llama.cpp/bin/llama-completion
pkg-snapdragon/llama.cpp/bin/llama-bench
pkg-snapdragon/llama.cpp/lib/libggml.so
pkg-snapdragon/llama.cpp/lib/libggml-base.so
pkg-snapdragon/llama.cpp/lib/libggml-cpu.so
pkg-snapdragon/llama.cpp/lib/libggml-hexagon.so
pkg-snapdragon/llama.cpp/lib/libggml-htp-v81.so
```

确认文件是 Android ARM64 和 Hexagon DSP 格式：

```bash
file pkg-snapdragon/llama.cpp/bin/llama-cli
file pkg-snapdragon/llama.cpp/lib/libggml-hexagon.so
file pkg-snapdragon/llama.cpp/lib/libggml-htp-v81.so
```

预期特征：

```text
llama-cli:          ELF 64-bit ... ARM aarch64 ... Android
libggml-hexagon.so: ELF 64-bit ... ARM aarch64 ... Android
libggml-htp-v81.so: ELF 32-bit ... QUALCOMM DSP6
```

如果当前只是普通主机版构建，`file` 通常会显示 `x86-64`，该程序不能直接在
Android 手机上运行。

安装 Snapdragon Android 构建结果：

```bash
cmake --install build-snapdragon --prefix pkg-snapdragon/llama.cpp
```

也可以使用仓库提供的 Snapdragon 构建脚本生成 Android 包：

```bash
./scripts/snapdragon/build.py \
  --target android \
  --build-dir build-snapdragon \
  --install-dir pkg-snapdragon
```

## 3. 推送程序和模型

本文使用 `/data/local/tmp`，不需要 root 权限。先创建目标目录：

```bash
adb shell 'mkdir -p /data/local/tmp/llama.cpp /data/local/tmp/gguf'
```

推送完整安装包：

```bash
adb push pkg-snapdragon/llama.cpp/. /data/local/tmp/llama.cpp/
```

推送模型：

```bash
adb push \
  /media/prj/gguf/SmolLM2-360M-Instruct-Q4_K_M.gguf \
  /data/local/tmp/gguf/
```

设置可执行权限：

```bash
adb shell 'chmod -R 755 /data/local/tmp/llama.cpp/bin'
```

检查推送结果：

```bash
adb shell 'ls -lh /data/local/tmp/llama.cpp/bin/llama-cli'
adb shell 'ls -lh /data/local/tmp/gguf/SmolLM2-360M-Instruct-Q4_K_M.gguf'
```

## 4. 关键环境变量

在手机上运行时必须设置：

```bash
LD_LIBRARY_PATH=./lib
ADSP_LIBRARY_PATH=./lib
```

`LD_LIBRARY_PATH` 用于加载 Android 主机侧动态库，例如：

```text
libggml.so
libggml-hexagon.so
libggml-cpu.so
```

`ADSP_LIBRARY_PATH` 用于让 Hexagon/Qualcomm DSP 侧找到：

```text
libggml-htp-v81.so
```

两个变量都需要设置。只设置 `LD_LIBRARY_PATH` 时，主程序可以启动，但 HTP
session 会创建失败，常见日志如下：

```text
ggml-hex: HTP0 failed to open session
error 0x80000406
```

## 5. 直接运行方式

进入安装目录并设置环境变量：

```bash
adb shell
cd /data/local/tmp/llama.cpp

export LD_LIBRARY_PATH=./lib
export ADSP_LIBRARY_PATH=./lib
```

运行一个短生成测试：

```bash
./bin/llama-completion \
  -m /data/local/tmp/gguf/SmolLM2-360M-Instruct-Q4_K_M.gguf \
  --device HTP0 \
  -ngl 99 \
  -t 4 \
  -c 512 \
  -b 128 \
  -ub 128 \
  -n 48 \
  -p "The capital of France is" \
  -no-cnv
```

参数说明：

| 参数 | 作用 |
| --- | --- |
| `-m` | GGUF 模型路径 |
| `--device HTP0` | 选择 Hexagon HTP0 |
| `-ngl 99` | 将可用的模型层尽可能卸载到 HTP |
| `-t 4` | CPU 侧线程数 |
| `-c 512` | 本次测试的上下文长度 |
| `-b 128` | prompt batch size |
| `-ub 128` | physical ubatch size |
| `-n 48` | 最多生成 48 个 token |
| `-no-cnv` | 不使用交互式 conversation 模式，直接按 prompt 生成 |

## 6. 使用 run.py 运行

推荐使用 `scripts/snapdragon/run.py`。它会自动：

- 通过 ADB 执行命令；
- 设置 `LD_LIBRARY_PATH=./lib`；
- 设置 `ADSP_LIBRARY_PATH=./lib`；
- 设置 `GGML_HEXAGON_DEVICES`；
- 将命令行设备参数转换为 llama.cpp 参数；
- 对常用工具补充合适的线程和 ubatch 默认值。

在主机上的仓库目录执行：

```bash
./scripts/snapdragon/run.py \
  --target adb \
  --devices HTP0 \
  -- \
  llama-completion \
  -m /data/local/tmp/gguf/SmolLM2-360M-Instruct-Q4_K_M.gguf \
  -ngl 99 \
  -t 4 \
  -c 512 \
  -b 128 \
  -ub 128 \
  -n 48 \
  -p "The capital of France is" \
  -no-cnv
```

使用 `run.py` 时，模型路径是手机路径，不是主机路径。模型仍然需要先用
`adb push` 推到手机。

如果手机连接了多个 ADB 设备，可以指定 serial：

```bash
./scripts/snapdragon/run.py \
  --target adb:c495c2c3 \
  --devices HTP0 \
  -- \
  llama-cli \
  -m /data/local/tmp/gguf/SmolLM2-360M-Instruct-Q4_K_M.gguf \
  -ngl 99 \
  -p "Hello"
```

## 7. HTP 执行验证

仅仅看到程序输出文本还不够。需要确认 HTP 参与执行，并检查哪些算子回退 CPU。可设置 Hexagon verbose 日志：

```bash
adb shell 'cd /data/local/tmp/llama.cpp && \
  export LD_LIBRARY_PATH=./lib ADSP_LIBRARY_PATH=./lib \
  GGML_HEXAGON_VERBOSE=1 && \
  ./bin/llama-completion \
  -m /data/local/tmp/gguf/SmolLM2-360M-Instruct-Q4_K_M.gguf \
  --device HTP0 -ngl 99 -fit off -t 4 -c 512 -b 128 -ub 128 -n 4 \
  -p "The capital of France is" -no-cnv --verbose' \
  > /tmp/opencode/smollm2-htp-verify.log 2>&1
```

检查关键日志：

```bash
grep -E "new session|offloaded|HTP0 model buffer|graph-compute|failed|error" \
  /tmp/opencode/smollm2-htp-verify.log
```

本次验证得到的关键日志：

```text
ggml-hex: HTP0 new session : session-id 0 domain-id 100000
load_tensors: offloaded 33/33 layers to GPU
load_tensors: HTP0 model buffer size = 109.28 MiB
ggml-hex: HTP0 graph-compute n_nodes 4
ggml-hex: HTP0 graph-compute n_nodes 13
```

这些日志表示：

1. HTP0 session 创建成功；
2. 加载器报告 33/33 层卸载，但这不是逐算子执行位置的保证；
3. HTP 侧已经分配模型 buffer；
4. 部分推理图通过 HTP backend 执行。

### 7.1 实际存在 CPU 回退

详细日志还包含：

```text
load_tensors: CPU model buffer size = 199.28 MiB
ggml-hex: HTP0 supports-op MUL_MAT|blk.0.attn_q.weight x attn_norm-0 -> Qcur-0|960:960 x 960:1 -> 960:1|q5_0 x f32 -> f32|22:660 x 4:3840 -> 4:3840|CPU x NONE -> NONE|no
```

模型虽命名为 Q4_K_M，但实际张量类型混合了 F32、Q5_0、Q8_0、Q4_K 和 Q6_K。其中 Q5_0 有 176 个张量，多个注意力和 FFN 矩阵乘法的 HTP 支持检查返回 `no`。因此本次应描述为 CPU/HTP 混合推理，不能声称没有 CPU fallback。CPU 缓冲大小也不能单独换算成 CPU 计算占比。

本次原始日志位于主机 `/tmp/opencode/smollm2-htp-run.log` 和 `/tmp/opencode/smollm2-htp-verify.log`。这些是临时文件，长期留档时应另行备份。复现日志命令前请确保 `/tmp/opencode` 已存在。

## 8. 实测结果

模型元数据：

| 项目 | 值 |
| --- | --- |
| 架构 | `llama` |
| 参数量 | 约 `361.82M` |
| 层数 | `32` |
| embedding | `960` |
| context length | `8192` |
| 文件类型 | `Q4_K - Medium` |
| 文件大小 | `256.35 MiB` |
| HTP offload | `33/33 layers` |
| HTP model buffer | `109.28 MiB` |
| CPU model buffer | `199.28 MiB` |
| 执行方式 | CPU/HTP 混合，存在 Q5_0 矩阵乘法回退 |

实际输出：

```text
The capital of France is Paris. Paris is a city located in the northern part of France...
```

本次测试参数：

```text
context       512
prompt batch  128
ubatch        128
threads       4
generated     48 tokens
```

本次实测性能：

```text
prompt eval: 16.05 tokens/s
generation:   11.62 tokens/s
```

实际性能会受手机温度、功耗模式、后台进程、上下文长度和 batch size 影响。
上面的结果用于确认部署和执行链路，不应直接作为该手机的最终性能上限。

## 9. 常见问题

### 9.1 `failed to open session` / `0x80000406`

本次补齐 `ADSP_LIBRARY_PATH` 后错误消失，但此错误码不唯一对应搜索路径问题。首先确认两个环境变量都设置了：

```bash
echo "$LD_LIBRARY_PATH"
echo "$ADSP_LIBRARY_PATH"
```

在通过 `adb shell` 直接运行时，完整形式应类似：

```bash
cd /data/local/tmp/llama.cpp
LD_LIBRARY_PATH=./lib \
ADSP_LIBRARY_PATH=./lib \
./bin/llama-cli ...
```

然后确认 v81 skel 库确实存在：

```bash
ls -l /data/local/tmp/llama.cpp/lib/libggml-htp-v81.so
```

### 9.2 日志显示 `HTP0` 但没有 offload

确认命令包含：

```text
--device HTP0
-ngl 99
```

同时检查是否使用了正确的 Android 包，而不是主机的 x86_64 包：

```bash
file pkg-snapdragon/llama.cpp/bin/llama-cli
```

### 9.3 `device HTP0 did not report memory`

部分设备或当前后端版本可能无法通过设备查询接口返回 HTP 可用内存，日志会出现：

```text
device HTP0 did not report memory
```

这会影响自动参数 fitting，但不一定影响实际推理。对于已知可运行的模型，
可以显式指定 `-ngl 99`；必要时加 `-fit off`，然后以是否出现以下日志为准：

```text
new session
offloaded N/N layers to GPU
HTP0 model buffer size
graph-compute
```

### 9.4 直接执行 `llama-cli` 时启动失败

不要只设置 `LD_LIBRARY_PATH`。直接执行时应同时设置：

```bash
export LD_LIBRARY_PATH=./lib
export ADSP_LIBRARY_PATH=./lib
```

也可以统一使用 `run.py`，避免遗漏 DSP 搜索路径。

### 9.5 模型路径找不到

`run.py --target adb` 执行的命令运行在手机上，因此 `-m` 参数必须使用手机路径：

```text
/data/local/tmp/gguf/SmolLM2-360M-Instruct-Q4_K_M.gguf
```

主机路径：

```text
/media/prj/gguf/SmolLM2-360M-Instruct-Q4_K_M.gguf
```

只能用于 `adb push`，不能直接传给手机上的 llama.cpp。

## 10. 重新部署与数据保护

不要删除整个 `/data/local/tmp/gguf`，其中可能还有其他模型。相同版本的包可按以下方式重新推送；切换构建版本时，优先使用新的独立安装目录，以免混用旧库。推送会覆盖同名文件，请先确认目标内容。

```bash
adb shell 'mkdir -p /data/local/tmp/llama.cpp /data/local/tmp/gguf'
adb push pkg-snapdragon/llama.cpp/. /data/local/tmp/llama.cpp/
adb push /media/prj/gguf/SmolLM2-360M-Instruct-Q4_K_M.gguf /data/local/tmp/gguf/
adb shell 'chmod -R 755 /data/local/tmp/llama.cpp/bin'
```

## 11. 后续模型测试建议

在 SmolLM2 基线验证通过后，可以按模型规模逐步测试其他 GGUF。每个模型都应
单独确认：

1. 能否创建 HTP session；
2. `offloaded N/N layers` 是否符合预期；
3. 是否出现 unsupported op 或 CPU fallback；
4. 输出文本是否正常；
5. HTP buffer 和上下文 buffer 是否满足手机内存限制。

建议先使用短上下文和少量生成 token，再逐步增加 `-c`、`-b` 和 `-n`，避免把
模型加载问题与长上下文内存问题混在一起。

## 12. SmolVLM2-256M 部署与准确性对照

### 12.1 模型与测试图片

主机模型目录：`/media/prj/gguf/SmolVLM2-256M/`。

```text
SmolVLM2-256M-Video-Instruct-Q4_K_M.gguf
mmproj-SmolVLM2-256M-Video-Instruct-Q8_0.gguf
```

手机模型目录：`/data/local/tmp/gguf/SmolVLM2-256M/`。

测试图片来自 `/home/bay/code/light/qnn/Tutorial_for_CLIP_IoT/example3/000000039769.jpg`，推送到手机后命名为 `cats.jpg`。图片为 640x480，两只猫躺在粉色织物上，旁边有遥控器。

这次没有重新训练或重新量化模型，也没有修改 HTP 源码。使用现有 `pkg-snapdragon/llama.cpp` 中的 Android `llama-mtmd-cli`。

### 12.2 已通过单张图片检查的配置

在主机执行：

```bash
adb shell '
cd /data/local/tmp/llama.cpp &&
export LD_LIBRARY_PATH=./lib ADSP_LIBRARY_PATH=./lib &&
./bin/llama-mtmd-cli \
  -m /data/local/tmp/gguf/SmolVLM2-256M/SmolVLM2-256M-Video-Instruct-Q4_K_M.gguf \
  --mmproj /data/local/tmp/gguf/SmolVLM2-256M/mmproj-SmolVLM2-256M-Video-Instruct-Q8_0.gguf \
  --image /data/local/tmp/gguf/SmolVLM2-256M/cats.jpg \
  --device HTP0 \
  --mmproj-device none \
  -ngl 99 -fit off -t 4 -c 8192 -b 512 -ub 128 -n 64 \
  --temp 0 \
  -p "Describe the image in one sentence." \
  --jinja --perf
'
```

`--mmproj-device none` 将视觉编码与投影放在 CPU，`--device HTP0` 为语言模型选择 HTP。语言模型仍可能存在 CPU 回退，不能称为纯 HTP LLM。

实际输出：

```text
Two tabby cats are resting on a pink blanket, with a remote control visible in the background.
```

这表明此图片的端到端结果合理，不是经过数据集评测的准确率结论。

### 12.3 视觉组件指定到 HTP 的结果

只将上述配置中的 projector 设备改为：

```text
--mmproj-device HTP0
```

固定 `--temp 0` 的对照输出为：

```text
A collage of images with a white background, featuring a mix of colors and patterns, is overlaid with a white wave-like pattern.
```

结果不符合图片内容。因此当前版本、设备和模型文件组合的视觉 HTP 路径未通过本次识图检查。不能将进程正常退出或出现 `HTP0 graph-compute` 视为准确性通过。

语言模型加载日志还报告：

```text
offloaded 31/31 layers to GPU
CPU model buffer size = 53.42 MiB
HTP0 model buffer size = 71.61 MiB
```

这些是语言模型的缓冲统计，不是整个视觉编码器加语言模型的内存总量，也不能证明所有算子均在 HTP 上执行。

### 12.4 上下文不足问题

最初的 `-c 512` 配置出现：

```text
failed to find a memory slot for batch of size 64
Unable to decode media chunk 15
```

改为 `-c 8192` 后完成推理。日志中的 `total = 27` 是混合文本与媒体 chunk 数，不能解释为 27 个视觉块。本例实际发生 13 次视觉 batch 编码，每批输出 64 个视觉 token，合计 832 个视觉 token；完整 prompt 统计为 878 token。

## 13. 官方文档的支持范围

本次查阅以下上游官方文档及当前使用的 fork 文档：

- [ggml-org Snapdragon README](https://github.com/ggml-org/llama.cpp/blob/master/docs/backend/snapdragon/README.md)
- [ggml-org 多模态 README](https://github.com/ggml-org/llama.cpp/blob/master/tools/mtmd/README.md)
- [ggml-org server 参数文档](https://github.com/ggml-org/llama.cpp/blob/master/tools/server/README.md)
- [Ethan-a2 Snapdragon README](https://github.com/Ethan-a2/llama.cpp/blob/master/docs/backend/snapdragon/README.md)

文档支持范围应分层理解：

| 内容 | 本次查阅结果 |
| --- | --- |
| llama.cpp 的 SmolVLM2 模型与 mmproj 支持 | 多模态 README 明确列出 SmolVLM2 |
| 指定 projector 执行设备 | 参数文档提供 `--mmproj-device DEVICE`，`none` 表示不卸载 |
| Hexagon HTP backend | Snapdragon README 提供构建、部署、运行和算子测试说明 |
| SmolVLM2 Q8_0 projector 在 HTP v81 上的准确性 | 未在所查文档中发现该组合的验证声明、CPU 数值对齐结果或评测报告 |

多模态 README 还说明该功能处于持续开发中，可能发生破坏性变化。支持设备选择的接口，不等于针对每个模型、量化类型和设备组合都已完成准确性验证；未找到验证声明，也不等于官方明确禁止 HTP 视觉推理。

本次运行的是 Ethan-a2 fork 的构建产物，不能把本地行为直接等同于所有上游版本。当前可确认的是：同一图片、同一对话提示、同一量化文件且使用贪心采样时，CPU 视觉组件结果合理，HTP 视觉组件结果错误。问题与视觉后端选择有关，但尚未通过中间 embedding 比较或逐算子对照确定根因。

Q8_0 文件在 CPU 上得到合理结果，所以不能仅归因于模型量化本身；具体算子、数值精度、数据布局或融合实现仍需独立验证。本次没有完成这些验证。

## 14. SmolVLM2 耗时记录与统计口径

### 14.1 区分原始测试与补跑

以下日志均位于主机 `/tmp/opencode/`。它们对应不同运行，不能拼成同一次推理的统计：

| 日志 | 用途 |
| --- | --- |
| `smolvlm2-htp-run.log` | 最初 `-c 512` 上下文不足的失败记录 |
| `smolvlm2-htp-success.log` | HTP 视觉组件完成运行，但回答错误；开启大量算子日志，最多生成 32 token |
| `smolvlm2-cpu-vision-htp.log` | 原始 CPU 视觉组件正确识图记录，没有完整性能汇总 |
| `smolvlm2-htp-vision-greedy.log` | HTP 视觉组件的 `--temp 0` 对照，回答错误 |
| `smolvlm2-cpu-vision-timing.log` | CPU 视觉组件低日志量计时补跑，主机墙钟 19.20 秒 |
| `smolvlm2-cpu-vision-timing-verbose.log` | CPU 视觉组件详细计时补跑，主机墙钟 21.07 秒 |

文件名中的 `success` 只表示那次进程完成，不表示准确性通过。所有时间均为单次样本，不是多轮平均值；温度、调度、日志量、缓存状态及输出长度都可能影响结果。

### 14.2 原始正确识图测试

从 `smolvlm2-cpu-vision-htp.log` 可读取：

- 第一次视觉 batch 开始：进程日志时间约 1.123 秒。
- 最后一次视觉 batch 编码结束：约 18.715 秒。
- 13 次视觉编码耗时合计：16,199 ms。
- 第一批 4,005 ms；后续每批约 1,009 至 1,026 ms。
- 日志没有记录完整生成耗时、总墙钟和最终性能汇总，因此无法精确恢复原始这次的全部阶段耗时。

### 14.3 详细计时补跑：CPU 视觉组件，语言模型选择 HTP

使用第 12.2 节参数，额外增加 `--verbose`，并在主机通过 `/usr/bin/time` 包裹 `adb shell`。结果仍是正确的两只猫描述。

| 统计项 | 实测值 | 口径 |
| --- | ---: | --- |
| `load time` | 517.57 ms | libllama 报告值，不作为所有组件加载时间之和 |
| 视觉 batch 编码合计 | 17,855 ms | 13 次 `mtmd batch encoding done` 的整数毫秒之和 |
| 图像 embedding 的 LLM decode 合计 | 1,381 ms | 13 次 `image decoded` 的整数毫秒之和 |
| `prompt eval time` | 19,601.99 ms / 878 token | 该 CLI 的 prompt 阶段统计，44.79 token/s |
| `eval time` | 281.83 ms / 20 runs | 自回归 decode 统计，70.96 token/s |
| `total time` | 20,256.64 ms | libllama 内部统计窗口 |
| 主机端墙钟 | 21.07 s | 包含 ADB、进程初始化和退出等开销 |

重要口径说明：

- `image decoded` 不是 JPEG 解码时间，而是把视觉 embedding 输入语言模型进行 decode/prefill 的时间。此前把它称为“图片解码”容易误导，这里予以纠正。
- `mtmd batch encoding done` 覆盖视觉 batch 编码阶段，不能称为单独 projector 线性层的耗时。mmproj 文件包含视觉编码与投影组件。
- `prompt eval`、上述分项及 `total time` 存在包含关系，不应全部相加。此 CLI 的 prompt 统计表现为包含视觉阶段的口径，不宜直接视为纯 LLM 算子执行时间。
- 20 runs 是自回归 decode 调用次数，不应直接当成用户可见输出 token 数的精确值；首 token 和结束 token 会影响计数关系。
- 大量 verbose 输出会影响耗时。另一次低日志量补跑的视觉编码合计为 16,200 ms，主机墙钟 19.20 秒。

本次详细补跑的 13 批视觉编码耗时，单位 ms：

```text
3994, 2630, 1019, 1032, 1028, 1024, 1014, 1015, 1018, 1022, 1017, 1018, 1024
```

对应 embedding 输入 LLM 的 decode 耗时，单位 ms：

```text
468, 79, 75, 76, 75, 75, 75, 76, 76, 75, 75, 78, 78
```

前两批编码明显较慢，但未单独测量初始化、缓存和调度成本，不能将原因确定为某一种首次运行开销。

### 14.4 HTP 视觉组件的诊断运行

以下数值来自 `smolvlm2-htp-success.log`，不是固定 `--temp 0` 的那次对照。该次开启 `GGML_HEXAGON_VERBOSE=1` 和 `--verbose`，回答错误：

| 统计项 | 实测值 |
| --- | ---: |
| `load time` | 828.57 ms |
| 13 次视觉 batch 编码合计 | 1,151 ms |
| 13 次图像 embedding 的 LLM decode 合计 | 5,143 ms |
| `prompt eval time` | 7,188.07 ms / 878 token，122.15 token/s |
| `eval time` | 897.93 ms / 31 runs，34.52 token/s |
| `total time` | 8,796.41 ms |

这些数据能说明该诊断运行的视觉编码阶段耗时较低，但不能作为“保持准确率时加速 15 倍”的结论。两次运行的日志量、生成长度和设备状态不同，且 HTP 视觉结果错误。应先定位准确性问题，再进行统一条件下的性能比较。

## 15. 当前可用方案与验证边界

当前推荐 `--mmproj-device none --device HTP0`，即 CPU 视觉编码与投影，语言模型选择 HTP 并允许 CPU 回退。该配置通过本次单张图片检查，主要耗时在 CPU 视觉编码阶段。

暂不建议将 `--mmproj-device HTP0` 用于要求可靠识图的场景。本次未进行数据集准确率评测、视觉 embedding 数值对齐、逐算子定位或纯 HTP 执行证明，也未确认上游是否已有对应修复。

## 16. SmolVLM2 HTP 视觉错误的根因定位进展

本节记录后续定位实验。测试模型仍为 `SmolVLM2-256M-Video-Instruct-Q4_K_M.gguf`，测试图片仍为 `cats.jpg`，提示词为 `Describe the image in one sentence.`，并固定 `--temp 0`。

### 16.1 关键隔离结果

首先将语言模型完全留在 CPU，只把 mmproj 放到 HTP：

```text
--mmproj-device HTP0 --device none -ngl 0
```

该配置仍输出错误的图片描述，例如：

```text
A photo with a striking contrast of colors and patterns, featuring a mix of white and black...
```

因此，错误不是语言模型 HTP 层卸载或 HTP 语言模型生成路径造成的。错误发生在视觉 mmproj HTP 图本身，或发生在视觉图输出被交给 CPU 语言模型之前。

### 16.2 算子回退实验

通过 `GGML_HEXAGON_OPFILTER` 将指定算子回退到 CPU：

| 回退过滤器 | 结果 |
| --- | --- |
| `IM2COL` | 错误 |
| `NORM` | 错误 |
| `GELU` | 错误 |
| `FLASH_ATTN_EXT` | 错误 |
| `MUL_MAT` | 错误 |
| `MUL` | 错误 |
| `^ADD$` | 正确识别两只猫 |

`^ADD$` 同时在 `--device HTP0` 的完整语言模型配置下恢复正确结果：

```text
Two tabby cats are resting on a pink blanket, with a remote control visible in the background.
```

这说明问题已经收敛到视觉图中的 HTP `ADD` 路径，或者 HTP ADD 执行造成的后续缓冲/调度状态影响。不能简单表述为“所有 HTP projector 都不准确”。当前已验证的可用绕过方式是：

```text
GGML_HEXAGON_OPFILTER=^ADD$
```

该绕过会显著降低性能，不能作为最终修复。

### 16.3 视觉图中疑似关键 ADD 形状

HTP verbose 日志显示视觉 encoder 的 ADD 主要是 F32、第二个输入为单行 bias 或同形状 residual 的广播/逐元素加法。例如：

```text
ADD| (reshaped) ... x v.patch_embd.bias -> patch_bias
768:1024 x 768:1 -> 768:1024

ADD|patch_bias x v.position_embd.weight -> pos_embed
768:1024 x 768:1024 -> 768:1024

ADD|attn_out-0 x pos_embed (reshaped) -> ffn_inp-0
768:1024 x 768:1024 -> 768:1024
```

视觉图使用 1024 行的 token batch，明显不同于目前基础 ADD 单元测试中覆盖的很多小尺寸 case。`test-backend-ops -b HTP0 -o ADD` 在当前设备上报告 80/80 通过，因此问题不是所有 ADD 输入都会失败，仍需加入该视觉图的具体 shape、stride、线程和 VTCM 情况进行复现。

### 16.4 已排除的假设

- `-c 512` 会导致视觉 batch 内存槽不足，但 `-c 8192` 后问题仍存在；不是当前根因。
- CPU projector 使用相同图片和 prompt 正常；不是图片文件或 prompt 格式问题。
- `--temp 0` 下仍稳定错误；不是采样随机性。
- `GGML_HEXAGON_OPFUSION=0`、`GGML_HEXAGON_MM_SELECT=1`、`GGML_HEXAGON_FA_SELECT=0` 和 `GGML_HEXAGON_RT_REPACK=0` 均未恢复准确性。
- 单独回退视觉图的 `MUL_MAT`、`NORM`、`GELU`、`FLASH_ATTN_EXT` 和 `MUL` 均未恢复准确性。

### 16.5 当前根因结论与剩余工作

当前最强结论是：

> SmolVLM2 的视觉 HTP 图中，F32 `ADD` 的特定执行路径产生了会破坏视觉特征的结果，或该路径暴露了相关的 DMA/VTCM/缓冲同步问题。将 ADD 回退 CPU 即可恢复识图，而只回退其他已测试算子不能恢复。

这仍是“定位到算子类别和绕过方式”，不是已完成的源码级修复。下一步需要：

1. 从视觉日志抽取每个 ADD 的完整 shape、stride、线程数和 VTCM 配置，按执行顺序编号。
2. 构造包含 `768 x 1024` F32 residual 和 bias 广播的 HTP backend 单元测试，覆盖真实 stride 与多线程配置。
3. 对视觉图每个 ADD 单独回退，定位第一个改变最终识图结果的 ADD 节点。
4. 在 CPU 与 HTP 之间读取该节点及其前后 tensor，比较最大误差、均方误差、NaN/Inf 和余弦相似度。
5. 检查 HTP ADD 的 DMA 双缓冲、row stride、broadcast 指针和 `dma_queue` flush 顺序；确认问题后再改代码。

诊断日志位于主机 `/tmp/opencode/`，包括：

```text
smolvlm2-add-fallback-htpllm.log
smolvlm2-exact-ADD.log
smolvlm2-exact-MUL.log
smolvlm2-cpullm-CPY.log
smolvlm2-cpullm-ADD_MUL.log
smolvlm2-htp-success.log
```

## 17. HTP 布局修复验证

### 17.1 根因

HTP 会将满足 tiled weight 条件的 F32/F16/BF16 权重转换为 F16 tiled layout。该布局对 `MUL_MAT` 是正确的，因为 HMX matmul kernel 按 tiled layout 读取；但 binary kernel 的输入读取是 flat row layout。

SmolVLM2 的 `v.position_embd.weight` 被 repack 后参与 F32 `ADD`，导致它被按错误的元素宽度和布局读取。第一处明显异常是：

```text
pos_embed = patch_bias + v.position_embd.weight
```

CPU 的输出 sum 为 `450.053986`，默认 HTP 输出 sum 为 `2224730.750000`。将相同权重按 F16 tiled 数据误解释为 F32 后，得到约 `2225368.94` 的量级，与该异常一致。

### 17.2 修复策略

没有关闭浮点 HMX，也没有禁用全部 ADD。`ggml_hexagon_supported_binary()` 现在保守拒绝以下组合：binary op 的任一输入是叶子 F32/F16 tensor，并且满足 HTP tiled weight 条件。叶子不等同于权重，因此该判据也可能回退普通输入；它不是完整的 tiled binary 支持。

这样：

- 量化和浮点 `MUL_MAT` 仍可使用现有 HMX/tiled kernel；
- 中间激活之间的 residual ADD 仍在 HTP 执行；
- 可能使用 tiled 权重存储的叶子输入会触发 binary op 的 CPU 回退；
- 不依赖 SmolVLM 或 tensor 名称特判。

### 17.3 手机验证

使用重新构建并安装的 `build-snapdragon` 包，在 Snapdragon SM8850 手机上测试：

```text
--mmproj-device HTP0 --device none -ngl 0
```

无 `GGML_HEXAGON_OPFILTER` 时：

- 输出：`Two cats are resting on a pink blanket, with a remote control visible in the background.`
- HTP projector + CPU LLM 的最终 embedding 与 CPU projector 对比：余弦相似度 `0.999887`，RMSE `0.086535`；
- 13 个视觉 batch 总耗时约 `1295 ms`。

完整 HTP 配置：

```text
--mmproj-device HTP0 --device HTP0 -ngl 99
```

也正确输出：

```text
Two tabby cats are resting on a pink blanket, with a remote control visible in the background.
```

该配置仍报告 `31/31` 个语言模型层卸载到 HTP。13 个视觉 batch 总耗时约 `1309 ms`，完整 prompt eval 约 `8094 ms`。

这证明修复没有把语言模型整体退回 CPU，也没有依赖 `^ADD$` 全局回退。后续仍需在更多模型和浮点 binary 权重用例上做回归测试，并评估特定设备上的 HMX 与 CPU fallback 性能差异。

该补丁下基础 ADD 测试报告 74/74 通过，原版本为 80/80；少执行的用例是支持范围收窄，不是新增的正确性验证。尚未补齐 WEIGHTS buffer 与 scheduler fallback 的专用回归测试，也未处理所有 view 或 runtime repack 组合，不应视为完整通用修复。

历史全局 ADD 回退日志的 13 个视觉 batch 总耗时为 `12983 ms`。当前局部回退日志为 `1309 ms`，但二者是不同轮次单次运行，不是控制温度等变量后的严格性能基准。embedding dump 每块覆盖同一文件，上述相似度仅代表最后一块。
