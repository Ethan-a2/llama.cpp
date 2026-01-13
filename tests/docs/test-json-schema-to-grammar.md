# test-json-schema-to-grammar.cpp 文档

主题：将 JSON Schema 转换为 GBNF/PEG grammar 的系统化测试。

- 核心概念、组件、API
  - 组件：`json-schema-to-grammar.h`、`nlohmann::json`、`llama-grammar.h`；
  - 概念：属性、类型、枚举、引用解析与格式化输出。

- 整体架构与内部机制
  - 解析 schema→展开引用→生成 grammar→断言与反向校验。

- 详细分析与典型使用场景
  - 将 API schema 直接约束模型输出；

- 设计优势与影响、关键要点
  - 优势：规范化输出、减少后处理复杂度；
  - 关键：递归结构与边界项。

- 练习题与验证方法
  1) 练习：创建一个嵌套对象与数组的 schema 并生成 grammar。
     - 验证：生成的 grammar 与手写格式一致。

- 后续建议与典型应用场景、应知应会
  - 构建与运行：`./build/bin/test-json-schema-to-grammar`。