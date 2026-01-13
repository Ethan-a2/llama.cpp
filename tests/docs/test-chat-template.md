# test-chat-template.cpp 文档

主题：聊天模板渲染与规范一致性测试。

- 核心概念、组件、API
  - 组件：`chat.h`、`common.h`、`llama.h`；
  - 概念：不同平台模板的换行、缩进、占位符处理与归一化。

- 整体架构与内部机制
  - 读取模板→归一化换行（Windows/Unix）→渲染→断言结果；
  - 通过正则校验关键片段。

- 详细分析与典型使用场景
  - 避免平台差异导致的提示词不一致；
  - 用于模板迁移与跨仓库协作。

- 设计优势与影响、关键要点
  - 优势：模板层稳定，减少下游误差；
  - 关键：换行归一化、空白处理。

- 练习题与验证方法
  1) 练习：新增带 CRLF 的模板并保证与 LF 渲染一致。

- 后续建议与典型应用场景、应知应会
  - 应知应会：Jinja/Minja 常见陷阱；
  - 构建与运行：
    - 构建：`cmake -B build && cmake --build build --config Release -j $(nproc)`
    - 运行：`./build/bin/test-chat-template`。