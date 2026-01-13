# test-tokenizer-0.cpp 文档

主题：基础 tokenizer 一致性测试与批量文本处理。

- 核心概念、组件、API
  - 组件：`llama.h`、`common.h`、`console.h`；
  - 概念：将输入文本分词，并与参考进行对比；可与 Python/HF Tokenizer 脚本联动。

- 练习题与验证方法
  1) 练习：准备一组包含空白、制表、Unicode 的文本集，比较 token 序列。
  2) 验证：与 `tests/test-tokenizer-0.py` 输出一致。

- 构建与运行：`./build/bin/test-tokenizer-0`。