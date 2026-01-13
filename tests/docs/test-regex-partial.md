# test-regex-partial.cpp 文档

主题：支持部分最终匹配的正则测试（regex-partial），确保流式/增量输入场景下的解析健壮性。

- 核心概念、组件、API
  - 组件：`regex-partial.h`、`common.h`；
  - 概念：在未完全匹配时给出“可成为最终匹配”的状态与结果。

- 整体架构与内部机制
  - 组合样例→执行正则→断言期望与实际输出一致；异常时打印详细 diff。

- 练习题与验证方法
  1) 练习：为 JSON 键/值的半成形串编写部分匹配表达式，验证增量解析。

- 构建与运行：`./build/bin/test-regex-partial`。