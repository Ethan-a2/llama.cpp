# peg-parser/test-basic.cpp 文档

主题：基础 PEG 组合子测试，覆盖字符类、转义、序列/选择等。

- 核心概念、组件、API
  - 组件：`peg-parser.h`、`chat-peg-parser.h`、`testing.h`；
  - 概念：通过 `common_peg_parser_builder` 构建 parser，并对多种输入进行断言。

- 整体架构与内部机制
  - 使用 `testing` 框架分组与过滤；
  - 对转义（\n、\t、\\）与字符区间进行验证。

- 练习题与验证方法
  1) 练习：添加一个组合语法 `p.seq(p.chars("[a-z]") , p.chars("[0-9]"))`。
     - 验证："a1" 成功，"aa"/"11" 失败。

- 后续建议与典型应用场景、应知应会
  - 构建与运行：`./build/bin/test-peg-parser basic` 或运行总入口 `test-peg-parser`。