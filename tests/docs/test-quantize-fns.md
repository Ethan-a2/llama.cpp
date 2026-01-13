# test-quantize-fns.cpp 文档

主题：量化/反量化与点积核函数的正确性测试。

- 核心概念、组件、API
  - 组件：`ggml.h`、`ggml-cpu.h`；
  - 概念：不同量化格式的误差度量（参考误差、总误差阈值）。

- 练习题与验证方法
  1) 练习：在合成数据上比较 q4_0/q5_1 的量化误差与点积输出。
  2) 验证：误差不超过 `MAX_QUANTIZATION_*` 常量设定。

- 构建与运行：`./build/bin/test-quantize-fns`。