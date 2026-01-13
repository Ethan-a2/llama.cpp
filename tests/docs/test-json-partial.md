# test-json-partial.cpp 文档

主题：JSON 部分解析与“自愈”策略测试，面向流式/不完整输入。

- 核心概念、组件、API
  - 组件：`json-partial.h`、`common.h`；
  - 概念：在输入不完整时进行修复与继续解析。

- 练习题与验证方法
  1) 练习：逐字符追加 JSON 并在半成形时尝试修复。

- 构建与运行：`./build/bin/test-json-partial`。