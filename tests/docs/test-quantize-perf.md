# test-quantize-perf.cpp 文档

主题：量化函数在不同数据规模上的性能基准。

- 核心概念、组件、API
  - 组件：`ggml.h`、`ggml-cpu.h`；
  - 概念：吞吐、内存访问模式、向量化影响。

- 练习题与验证方法
  1) 练习：改变块大小与数据长度，记录时间与带宽估计。

- 构建与运行：`./build/bin/test-quantize-perf`。