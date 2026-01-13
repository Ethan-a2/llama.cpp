# test-quantize-stats.cpp 文档

主题：量化统计、分布与对模型层的影响分析。

- 核心概念、组件、API
  - 组件：`ggml.h`、`ggml-cpu.h`、`llama.h`、`common.h`；
  - 概念：层级统计、直方图、误差累计对输出质量的影响。

- 练习题与验证方法
  1) 练习：对不同层应用不同量化格式，比较困惑度/输出质量。

- 构建与运行：`./build/bin/test-quantize-stats`。