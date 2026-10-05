# ADR-001：v0.2 使用 LibIME，独立管理个人学习

状态：采用。目标环境 Ubuntu 24.04；最终依赖版本和测试结果见 `validation.md`。

需求原本优先考虑 libpinyin，并要求 Phase 0 与 LibIME 对照。两者都提供统计式拼音算法，libpinyin 也提供整句与学习接口，不能将它描述成单纯的无状态候选生成器。

选择 LibIME 的原因：

1. 它是 Fcitx5 官方拼音插件使用的后端；输入缓冲、光标、逐段选择和双拼接口可直接满足 v0.2 的主要输入能力。
2. `PinyinContext` 能把候选关联到 lattice 的原始输入终点，并支持选择后的消费范围与撤销。项目重排时保留 `backendIndex`，UI 使用修订号和候选 ID 回传，避免把显示下标当成后端下标。
3. 系统词库及模型来自 Ubuntu 的 `libime-data`，独立引擎不复制或覆盖官方拼音插件的用户数据。
4. 对 libpinyin 的接口及包可获得性做初步核验，但本次不维护两套生产后端，也不声称已完成两者输入质量的全面实测对比。

## 项目层与后端的边界

Core 不引用 Fcitx5 Core 的输入上下文/事件类型；LibIME 自身依赖 Fcitx Utils，这个底层工具依赖不会伪装成完全无依赖。Addon 转换框架事件、配置和 UI，Core 管理组合输入、候选快照和学习事件。

项目不调用 `PinyinContext::learn()`，也不保存 LibIME 原生 history 模型。个人学习只使用 SQLite 中的词条/统计和项目的有界提升，避免与后端 history 重复累加；句子原始解码仍使用后端基础语言模型。关闭学习禁止新学习，隐私模式使用不包含个人/领域词库的独立基础后端。

这个取舍让导出、清空、备份和故障恢复只有一份数据源。代价是没有复用后端的个人 bigram 学习；上下文学习属于后续阶段。

## 线程与数据更新

- 后端会话对象和已发布的引擎仅由 Fcitx5 主事件循环使用。
- 数据库线程负责写入和只读快照，学习事件携带数据库代际；清空后旧事件不会写回。
- 导入和禁用词库通过事务更新 SQLite 元数据。候选后端在后台独立构建，完成后由事件循环接收。
- 普通学习刷新保留进行中的组合及候选映射；清空/导入/禁用等显式词库变更使旧会话失效。新词库一般在数秒内生效。
- 不记录周围文本，日志不含输入内容。数据库错误保留原文件并继续基础输入。

## v0.2 的质量边界

拼音纠错是有界的常见拼写、末尾同行邻键和末尾换位搜索，不是任意位置的通用纠错；emoji 是小型固定候选表。领域示例词库各 5 条，不代表完整商业词库。双拼首批仅自然码、小鹤、微软。

基础字词与长句效果受系统词库、语言模型及候选上限影响。图形会话兼容与输入质量需按实际目标系统验收，不能仅靠单元测试声称达到搜狗的整体效果。

## 来源与授权清单

| 依赖/资源 | 来源 | 授权 / 分发 |
| --- | --- | --- |
| Fcitx5 Core/Config/Utils | Ubuntu 包，上游 fcitx/fcitx5 | LGPL-2.1-or-later 等，以包 copyright 为准；不捆绑库副本 |
| LibIME | Ubuntu `libimepinyin0` / `libimepinyin-dev` | LGPL-2.1-or-later 等，以包 copyright 为准；动态链接 |
| LibIME 词库和模型 | Ubuntu `libime-data` 及关联数据包 | 由 Ubuntu 仓库分别分发，许可见各包 copyright |
| OpenCC | Ubuntu 包，上游 BYVoid/OpenCC | Apache-2.0 等，以包 copyright 为准；动态链接 |
| SQLite | Ubuntu 包 | Public domain 等，以包 copyright 为准；动态链接 |
| GoogleTest | Ubuntu 开发包 | BSD-3-Clause，测试构建使用，不进入安装包 |
| Python/Tk | Ubuntu 运行时包 | 以各包 copyright 为准，不捆绑 |
| 本项目源码与示例词库 | 本仓库、人工编写词条 | GPL-3.0-or-later |

上游依据：[Fcitx5 官方拼音构建](https://github.com/fcitx/fcitx5-chinese-addons/blob/master/im/pinyin/CMakeLists.txt)、[LibIME PinyinContext](https://github.com/fcitx/libime/blob/master/src/libime/pinyin/pinyincontext.h)、[libpinyin 公共接口](https://github.com/libpinyin/libpinyin/blob/main/src/pinyin.h)。实现只使用 Ubuntu 24.04 实际提供的 API，而不是直接照搬上游最新版本。
