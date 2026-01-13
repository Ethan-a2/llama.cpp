# test-thread-safety.cpp 文档

主题：多模型、多上下文并行推理的线程安全验证，覆盖 CPU 与各 GPU 后端。

- 核心概念、组件、API
  - 并行度：`--parallel` 指定每模型的并发上下文数；
  - 组件：`llama.h`、`arg.h`、`common.h`、`sampling.h`；
  - 负载：在每个上下文中执行推理循环。

- 整体架构与内部机制
  - 为每个设备加载一份模型（含 CPU）；
  - 在每份模型上创建 N 个上下文并发运行；
  - 原子计数/日志用于监控与终止；

- 详细分析与典型使用场景
  - 服务端每卡多会话；桌面应用多窗口；
  - 注意：KV cache 容量、内存/显存峰值；

- 设计优势与影响、关键要点
  - 优势：提前暴露竞态与死锁；
  - 影响：指导资源隔离与负载均衡；
  - 关键：每上下文独立、避免跨线程共享非线程安全对象。

- 练习题与验证方法
  1) 练习：将 `--parallel` 设为不同值，记录吞吐与稳定性。
     - 验证：CPU/GPU 均无崩溃，吞吐随并发合理变化。
  2) 练习：开启高并发并降低 batch size，观察调度影响。

- 后续建议与典型应用场景、应知应会
  - 建议：结合后端专用 profiler；
  - 应知应会：`llama_*` 上下文的线程安全边界；
  - 构建与运行：
    - 构建：`cmake -B build && cmake --build build --config Release -j $(nproc)`
    - 运行：`./build/bin/test-thread-safety --parallel 4 --model /path/to/model.gguf`。