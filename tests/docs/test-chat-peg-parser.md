# test-chat-peg-parser.cpp 文档

主题：基于 PEG 的聊天解析与 grammar 生成测试，覆盖工具调用与 JSON 格式。

- 核心概念、组件、API
  - 组件：`chat-peg-parser.h`、`chat-parser.h`、`peg-parser.h`、`json-schema-to-grammar.h`；
  - 概念：用 PEG 表达式构造聊天语法，生成/序列化/反序列化 grammar，并进行解析。

- 整体架构与内部机制
  - 通过 `common_peg_parser_builder` 组合语法；
  - 将工具调用表示为 JSON，支持 to_json / from_json；
  - 对比 expected/actual 以断言语法与解析一致。

- 详细分析与典型使用场景
  - 适用于需要强约束输出结构的对话（如函数调用、表格）；
  - 易于将 schema 转 grammar 用于推理约束。

- 设计优势与影响、关键要点
  - 优势：强一致性；
  - 影响：语法过强可能降低生成自由度；
  - 关键：转义与 Unicode、可选项与重复项的组合。

- 练习题与验证方法
  1) 练习：新增一个工具调用 schema，并生成对应 PEG grammar。
     - 验证：parse 成功，序列化/反序列化后等价。

- 后续建议与典型应用场景、应知应会
  - 建议：与 server JSON tool calling 接口核对；
  - 应知应会：GBNF/PEG 差异、nlohmann::json 的有序/无序行为。
  - 构建与运行：
    - 构建：`cmake -B build && cmake --build build --config Release -j $(nproc)`
    - 运行：`./build/bin/test-chat-peg-parser`。