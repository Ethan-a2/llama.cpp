# test-tokenizer-1-spm.cpp 文档

主题：SentencePiece 分词器路径与结果一致性测试。

- 核心概念、组件、API
  - 组件：`llama.h`、`common.h`、`console.h`、`unicode.h`；
  - 概念：SPM 模型加载、Unicode 正规化。

- 练习题与验证方法
  1) 练习：在包含多脚本（中/英/俄/阿）文本上验证分词一致性。

- 构建与运行：`./build/bin/test-tokenizer-1-spm <dir_tokenizer>`。