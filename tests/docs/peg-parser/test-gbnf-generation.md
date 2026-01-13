# peg-parser/test-gbnf-generation.cpp 文档

主题：从 JSON Schema/描述构建 grammar 并生成 GBNF，验证格式一致性。

- 核心概念、组件、API
  - 组件：`json-schema-to-grammar.h`、`testing.h`、`peg-parser.h`；
  - 概念：trim/格式化，预期/实际比较。

- 练习题与验证方法
  1) 练习：生成一个包含可选字段与数组的 grammar，并比较输出。

- 构建与运行：`./build/bin/test-peg-parser gbnf-generation`。