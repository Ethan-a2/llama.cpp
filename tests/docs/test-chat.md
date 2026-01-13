# test-chat.cpp 文档

主题：聊天管线的端到端测试（模板、grammar、tokenizer、采样）与 Markdown 摘要生成 CLI。

- 核心概念、组件、API
  - 组件：`chat.h`、`log.h`、`llama-grammar.h`、`unicode.h`、`nlohmann::json`；
  - 概念：模板渲染→tokenize→grammar 约束→采样，形成完整聊天回路。

- 整体架构与内部机制
  - 加载模板与工具调用格式；
  - 根据对话上下文生成提示词并约束 grammar；
  - 进行采样与结果校验；

- 详细分析与典型使用场景
  - 生成对话/工具调用；不同模板（Jinja/Minja）格式测试；
  - CLI：可输出模板的 Markdown 摘要以便对比与文档化。

- 设计优势与影响、关键要点
  - 优势：端到端覆盖，便于发现跨模块问题；
  - 关键：模板一致性、grammar 生成的正确性、tokenizer 差异处理。

- 练习题与验证方法
  1) 练习：新增一个模板，确保工具调用与角色格式正确。
     - 验证：对话生成稳定，Markdown 摘要与预期一致。
  2) 练习：在 grammar 中限制输出 JSON 结构，验证采样不越界。

- 后续建议与典型应用场景、应知应会
  - 建议：与 server（tools/server）联动测试 API 输出；
  - 应知应会：模板规范、grammar 语法、采样器配置。
  - 构建与运行：
    - 构建：`cmake -B build && cmake --build build --config Release -j $(nproc)`
    - 运行：`./build/bin/test-chat <templates/*.jinja>`（参考源码注释示例）。