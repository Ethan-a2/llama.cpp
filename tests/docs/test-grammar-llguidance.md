# test-grammar-llguidance.cpp 文档

主题：LLGuidance 风格的 grammar 与采样器集成测试，验证字符串匹配与受限生成。

- 核心概念、组件、API
  - 组件：`sampling.h`、`llama_vocab`；
  - 概念：根据 grammar 约束对输入进行匹配并对采样结果做校验。

- 整体架构与内部机制
  - 初始化 `vocab` 与 `llama_sampler`；
  - 用 `common_tokenize` 将输入转成 tokens，就地重置采样器并匹配字符串。

- 练习题与验证方法
  1) 练习：构造只接受特定前缀/后缀的 grammar，验证匹配与生成。

- 构建与运行：`./build/bin/test-grammar-llguidance`。