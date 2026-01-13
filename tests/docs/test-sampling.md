# test-sampling.cpp 文档

主题：采样器（含 dry 模式）行为与分布校验。

- 核心概念、组件、API
  - 组件：`llama.h`、采样器初始化函数；
  - 概念：top-k/top-p/temperature 与 dry 模式的交互。

- 练习题与验证方法
  1) 练习：固定 logits 与参数设置，比较不同策略的输出分布。

- 构建与运行：`./build/bin/test-sampling`。