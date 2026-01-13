# test-grammar-integration.cpp 文档

主题：JSON schema → grammar（GBNF）→ llama 内部 grammar 的端到端集成验证。

- 核心概念、组件、API
  - 组件：`json-schema-to-grammar.h`、`llama-grammar.h`、`unicode.h`、`nlohmann::json`；
  - 概念：从 schema 生成 grammar，再转为内部表示后进行解析与约束生成。

- 整体架构与内部机制
  - 构造/读取 schema→生成 grammar 字符串→`llama_grammar_init`→受限解析；
  - 用断言覆盖必填、枚举、类型匹配等。

- 详细分析与典型使用场景
  - 函数调用/结构化输出；

- 设计优势与影响、关键要点
  - 优势：工程上可直接将 API schema 用于推理约束；
  - 关键：引用与递归、可选项处理。

- 练习题与验证方法
  1) 练习：在 schema 中添加 `oneOf`/`anyOf`，观察 grammar 生成与解析效果。

- 后续建议与典型应用场景、应知应会
  - 构建与运行：`./build/bin/test-grammar-integration`。