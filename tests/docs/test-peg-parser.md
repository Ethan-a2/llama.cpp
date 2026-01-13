# test-peg-parser.cpp 文档

主题：PEG 测试总入口，聚合 basic/gbnf/json/unicode 等子测试。

- 使用
  - 通过环境变量 `LLAMA_TEST_VERBOSE=1` 开启详细日志；
  - 过滤参数选择子测试：`./build/bin/test-peg-parser <filter>`。

- 验证
  - 所有子测试通过，解析结果与期望一致。