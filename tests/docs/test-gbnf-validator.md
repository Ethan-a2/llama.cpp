# test-gbnf-validator.cpp 文档

主题：对外部提供的 GBNF grammar 的验证与字符级接受流程测试。

- 核心概念、组件、API
  - 组件：`llama-grammar.h`、`unicode.h`；
  - 概念：逐 code point 接受，错误位置与信息输出。

- 整体架构与内部机制
  - 将输入 UTF-8 转 code points；
  - 逐步 `llama_grammar_accept` 推进；
  - 记录错误位置与消息，断言结果。

- 详细分析与典型使用场景
  - 在生成受限输出时提前验证 grammar；

- 设计优势与影响、关键要点
  - 关键：Unicode 分解与回溯。

- 练习题与验证方法
  1) 练习：构建一个只接受数字的 grammar，并输入混合字符串。
     - 验证：错误位置与消息合理。

- 后续建议与典型应用场景、应知应会
  - 构建与运行：`./build/bin/test-gbnf-validator`。