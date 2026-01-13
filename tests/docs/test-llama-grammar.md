# test-llama-grammar.cpp 文档

主题：llama 内部 grammar 的规则计数与结构验证。

- 核心概念、组件、API
  - 组件：`llama.h`、`../src/llama-grammar.h`；
  - 概念：解析 grammar 并检查生成的规则数量与名称。

- 整体架构与内部机制
  - 使用 `llama_grammar_parser` 对语法进行解析，验证多个规则（`expr`、`expr_6` 等）。

- 练习题与验证方法
  1) 练习：新增规则并更新预期表，验证计数与名称一致。

- 构建与运行：`./build/bin/test-llama-grammar`。