# 词库格式

UTF-8 TSV：每行三个字段，用实际制表符分隔，不是多个空格：

```text
# novapinyin-dict-v1
工厂常数	gong chang chang shu	100
```

字段为文字、无声调小写全拼、权重（0..100000）。音节以单空格分隔，`ü` 写作 `v`。文字字符数与音节数必须一致，多音词分行提供不同读音；emoji 使用内置候选，不混入这个格式。`#` 开头为注释。单行上限 2048 字节、文件上限 8 MiB、每库上限 50000 个词条，启用词条总数上限 100000。

输入词库使用完整全拼，即使当前设置为双拼。导入前验证全部内容，同名导入以事务替换，失败保留原库。重复的同读音同文字行采用最后一行。词库管理器支持启用、禁用、移除、安装示例词库、导出个人词条和清空个人学习。

命令行工具：

```bash
novapinyin-tool validate my-dictionary.tsv
novapinyin-tool import my-domain my-dictionary.tsv
novapinyin-tool list
novapinyin-tool disable my-domain
novapinyin-tool enable my-domain
novapinyin-tool export personal.tsv
novapinyin-tool remove my-domain
novapinyin-tool clear --yes
```

导出的个人词条可作为一个词库重新导入；导出不包含输入历史或第三方词库。禁用一个领域词库不会删除此前已经学入个人词条的文字，需要移除所有个人数据时另行清空学习。

附带示例词库是本项目人工编写的小词表，不代表完整行业词库。原生基础词库/语言模型由发行版 `libime-data` 提供，不把商业输入法词库作为来源。
