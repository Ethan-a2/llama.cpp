# test-state-restore-fragmented.cpp 文档

主题：在碎片化 KV cache 下的状态恢复测试（issue 17527 修复验证）。

- 核心概念、组件、API
  - 组件：`llama.h`、`common.h`、`arg.h`；
  - 概念：允许非连续 slot 分配的恢复策略。

- 练习题与验证方法
  1) 练习：构造多段生成→保存→恢复→继续生成，检查一致性。

- 构建与运行：`./build/bin/test-state-restore-fragmented`。