# test-tokenizer-1-bpe.cpp 文档

主题：BPE 分词器的多平台一致性与边界处理测试。

- 核心概念、组件、API
  - 组件：`llama.h`、`common.h`、`console.h`、`unicode.h`；
  - 概念：字节级/子词级分割、特殊符号与语言混合。

- 练习题与验证方法
  1) 练习：在不同区域设置/locale 下比较分词结果。

- 构建与运行：`./build/bin/test-tokenizer-1-bpe <dir_tokenizer>`。