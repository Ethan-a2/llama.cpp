# test-double-float.cpp 文档

主题：double→float 转换在 ggml 关键函数中的影响分析与全空间验证。

- 核心概念、组件、API
  - 组件：向量化指令（SSE/AVX 等）、数学函数；
  - 概念：所有有限浮点检查，忽略 NaN/Inf。

- 练习题与验证方法
  1) 练习：对特定函数比较 double/float 结果，统计差异分布。

- 构建与运行：`./build/bin/test-double-float`。