# test-tokenizers-repo.sh 文档

主题：从外部 Git 仓库拉取 tokenizer 资源并批量验证。

- 使用
  - `./tests/test-tokenizers-repo.sh <git-repo> <target-folder> [<test-exe>]`
  - 自动调用 `test-tokenizer-0` 或指定测试程序。

- 验证
  - 检查可执行存在与权限；批量运行并汇总结果。