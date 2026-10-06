# 随包词库的来源与转换

词库由设置页的“本地词库 → 安装内置词库…”显式安装，安装后默认启用，可禁用或移除。运行时不联网更新；不会自动写入个人学习记录。

| 词库 | 来源与固定版本 | 许可 | 导出规模 |
| --- | --- | --- | --- |
| Rime 常用词精选 | [rime/rime-pinyin-simp](https://github.com/rime/rime-pinyin-simp/tree/0c6861ef7420ee780270ca6d993d18d4101049d0)，`pinyin_simp.dict.yaml` | Apache-2.0 | 20,000 条 |
| 雾凇社区补充词 | [iDvel/rime-ice](https://github.com/iDvel/rime-ice/tree/da1fbe602e38f26db846fa10120ee64c2b0324c0)，`cn_dicts/others.dict.yaml` | GPL-3.0 | 162 条 |

常用词原始数据注明派生自 Android Open Source Project 的 PinyinIME。作者及原始注释保留在 `upstream/rime-common/dictionary.yaml`；完整许可位于对应目录的 `LICENSE`。社区补充词保留 iDvel/rime-ice 原始注释和许可。这里只使用其补充文件，不包含整个雾凇方案或其外部大词库。

两份原始文件、许可、固定提交和 SHA256 随包保留，详见 `sources.json`。NovaPinyin 转换为自己的 TSV 格式，并修改了数据：仅保留 2–16 个汉字、字符数与全拼音节数一致的条目；跳过英文、表情及社区补充中的“错音错字”纠正区；将 `ü`、`lue/nue` 规范为 `v`、`lve/nve`。无数字权重的条目使用 100，其他权重限制在 0–100000。同词同读音合并，按权重精选最多 20,000 条，最终排序固定。转换不是完整 Rime 功能移植，也不宣称包含所有网络热词。

从仓库根目录运行 `python3 scripts/convert-bundled-dictionaries.py` 可校验来源并重建两份 TSV 与 `manifest.json`。C++ 测试使用实际导入校验器读取生成文件；管理器图形验证导入和启停。
