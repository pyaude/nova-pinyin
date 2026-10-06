# NovaPinyin — Ubuntu 本地拼音输入法

基于 Fcitx5 和 LibIME 的独立输入法引擎，当前版本为 **v1.0.0**，分别提供 Ubuntu 20.04、24.04 amd64 正式版安装包。使用 Fcitx5 的候选 UI 与配置工具；个人词条在本机保存。

可下载的发布包及校验文件以 GitHub Release 为准；不同 Ubuntu 版本的安装包不能混用。本地验证及仍需桌面验收的范围见 [验证记录](docs/validation.md)。

## 功能

- 全拼、基础句子组合、逐段选择、光标编辑、横向拼音候选、数字/空格/鼠标选词及候选翻页（↓ 下一页、↑ 上一页）。
- 自然码、小鹤、微软双拼；独立可配置的常用模糊音。
- 后端常见拼音纠错及末尾邻键/换位纠错；候选标记，不自动改写或执行命令。
- SQLite 本地词条学习、重启召回、关闭学习及隐私模式。
- 用户/领域词库导入、启用/禁用、移除与个人词条导出；随包提供 Rime 常用词精选（20,000 条）、雾凇社区补充词（162 条）和少量半导体、编程示例词库。
- OpenCC 繁体输出、有限的常用 emoji 候选。
- NovaPinyin设置（双拼、词库、浅色／深色候选配色）、Fcitx5 原生设置、Ubuntu `.deb` 打包。
- 默认关闭的内存上下文排序、可覆盖的应用提示；显式命令/代码补全及用户指定的项目标识符索引。

AI、编辑器语义集成、云输入和同步不在 v1.0 交付范围内。NovaPinyin设置不需要联网。新功能的边界及使用方法见 [高级输入说明](docs/advanced-input.md)。

## 安装

从 [v1.0.0 GitHub Release](https://github.com/pyaude/nova-pinyin/releases/tag/v1.0.0) 下载与你的系统对应的安装包及 `SHA256SUMS`，保存到同一目录：

| 系统 | 安装包 | 首次启用 |
| --- | --- | --- |
| Ubuntu 20.04 amd64（X11） | `novapinyin_1.0.0-1.ubuntu20.04.1_amd64.deb` | 安装后注销重登录或重启，自动设置 |
| Ubuntu 24.04 amd64 | `novapinyin_1.0.0-1.ubuntu24.04.1_amd64.deb` | im-config 选择 Fcitx5，重登录后添加 NovaPinyin |

以下是 Ubuntu 24.04 示例；只下载所需资产时用 `--ignore-missing` 检查现有文件。

```bash
sha256sum --ignore-missing --check SHA256SUMS
sudo apt install ./novapinyin_1.0.0-1.ubuntu24.04.1_amd64.deb
```

运行 `im-config` 选择 Fcitx5、注销重登录，再通过“Fcitx 5 配置”添加 NovaPinyin 拼音。完整步骤、GNOME/Wayland 注意事项和卸载恢复见 [安装说明](docs/install.md)。

Ubuntu 20.04 amd64 提供单独构建，带专用 Fcitx5/LibIME 运行库，安装后在下次 X11 登录自动完成输入法选择、环境配置和启动，无需手动执行 im-config 或添加输入法。仍需注销重登录或重启一次。安装方式和验证边界见 [Ubuntu 20.04 安装说明](docs/install.md#ubuntu-2004-专用包)。不能复用上面的 24.04 安装包；当前 20.04 构建针对 X11，Release 同时提供包含专用运行库及模型源数据的完整匹配源码归档。

## 开发与打包

在 Ubuntu 24.04 上安装构建依赖：

```bash
sudo apt-get install build-essential cmake ninja-build pkg-config \
  libfcitx5core-dev libfcitx5config-dev libimecore-dev libimepinyin-dev libime-data \
  libboost-dev libsqlite3-dev libopencc-dev libgtest-dev dpkg-dev python3-tk git
./scripts/build-deb.sh
```

脚本先构建和测试，再根据真实库依赖生成 `dist/novapinyin_1.0.0-1~ubuntu24.04.1_amd64.deb` 与 SHA256 校验文件。本地构建文件名保留 Debian 版本中的 `~`；Release 文件名规范为点，以保持 GitHub 资产名及校验文件一致。只需要构建时：

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

[需求文档](docs/base-requirements.md) · [后端决策](docs/backend-decision.md) · [词库格式](docs/dictionary-format.md) · [验证记录](docs/validation.md)

## 隐私与数据

运行时不发送按键、词条、周围文本或历史。默认仅保存被实际提交的个人词条、读音、选择次数与最近使用时间；不保存完整输入历史。用户显式索引后另存项目根路径、标识符及源码频次，个人词条导出不包含这些数据。上下文启用后仅在内存保留最近 128 字符。隐私模式使用独立基础模型，关闭个人词条、学习、上下文及开发者补全。密码/敏感提示来自应用，手动隐私模式用于补充。

学习数据库默认 `~/.local/share/novapinyin/user.db`，遵循 `XDG_DATA_HOME`；配置默认 `~/.config/fcitx5/conf/novapinyin.conf`，遵循 `XDG_CONFIG_HOME`。普通卸载保留用户数据。

## 授权

项目源码和人工编写的示例词库：GPL-3.0-or-later。Fcitx5、LibIME、OpenCC、SQLite、Python/Tk 等由发行版安装，遵循各自许可证；基础词库与模型使用发行版的 LibIME 数据包。依赖与来源见 [后端决策](docs/backend-decision.md)。

Ubuntu 20.04 专用包例外：自带 Fcitx5、LibIME、Qt 绑定、配置工具和所需数据，使用上游固定版本的源码与模型构建；完整匹配源码归档及许可说明随包提供。其他系统库仍由发行版提供。

随包词库及候选窗图标另按其原始许可提供，固定来源、原始数据和转换说明见 [词库来源](data/dictionaries/SOURCES.md) 与 [主题来源](data/themes/SOURCES.md)。
