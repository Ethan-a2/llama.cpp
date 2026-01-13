# peg-parser/test-json-serialization.cpp 文档

主题：PEG grammar 的 JSON 序列化与反序列化一致性测试。

- 核心概念、组件、API
  - 组件：`peg-parser.h`、`nlohmann::json`、`testing.h`；
  - 概念：`to_json`/`from_json` 的等价性、复杂输入一致性。

- 练习题与验证方法
  1) 练习：序列化包含嵌套结构的 grammar 并还原，比较 parse 结果。

- 构建与运行：`./build/bin/test-peg-parser json-serialization`。