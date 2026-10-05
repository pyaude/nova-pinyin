# NovaPinyin — Ubuntu 本地拼音输入法

基于 Fcitx5 和 LibIME 的独立输入法引擎，当前交付版本为 **v0.2.0**，目标 Ubuntu 24.04 amd64。使用 Fcitx5 的候选 UI 与配置工具；个人词条在本机保存。

## 功能

- 全拼、基础句子组合、逐段选择、光标编辑、数字/空格/鼠标选词及候选翻页。
- 自然码、小鹤、微软双拼；独立可配置的常用模糊音。
- 后端常见拼音纠错及末尾邻键/换位纠错；候选标记，不自动改写或执行命令。
- SQLite 本地词条学习、重启召回、关闭学习及隐私模式。
- 用户/领域词库导入、启用/禁用、移除与个人词条导出；附带少量半导体和编程示例词库。
- OpenCC 繁体输出、有限的常用 emoji 候选。
- 中文输入设置与词库管理器、Fcitx5 原生设置、Ubuntu `.deb` 打包。

AI、项目索引、编辑器代码补全、云输入和同步不在本次 v0.2 交付范围内。词库管理器不需要联网。

## 安装

```bash
sudo apt install ./novapinyin_0.2.0-1_amd64.deb
```

运行 `im-config` 选择 Fcitx5、注销重登录，再通过“Fcitx 5 配置”添加 NovaPinyin 拼音。完整步骤、GNOME/Wayland 注意事项和卸载恢复见 [安装说明](docs/install.md)。

## 开发与打包

在 Ubuntu 24.04 上安装构建依赖：

```bash
sudo apt-get install build-essential cmake ninja-build pkg-config \
  libfcitx5core-dev libfcitx5config-dev libimecore-dev libimepinyin-dev libime-data \
  libboost-dev libsqlite3-dev libopencc-dev libgtest-dev dpkg-dev python3-tk
./scripts/build-deb.sh
```

脚本先构建和测试，再根据真实库依赖生成 `dist/novapinyin_0.2.0-1_amd64.deb` 与 SHA256 校验文件。只需要构建时：

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

[需求文档](docs/base-requirements.md) · [后端决策](docs/backend-decision.md) · [词库格式](docs/dictionary-format.md) · [验证记录](docs/validation.md)

## 隐私与数据

运行时不发送按键、词条、周围文本或历史。默认仅保存被实际提交的个人词条、读音、选择次数与最近使用时间；不保存完整输入历史。隐私模式使用独立基础模型，关闭个人词条和学习。密码/敏感提示来自应用，手动隐私模式用于补充。

学习数据库默认 `~/.local/share/novapinyin/user.db`，遵循 `XDG_DATA_HOME`；配置默认 `~/.config/fcitx5/conf/novapinyin.conf`，遵循 `XDG_CONFIG_HOME`。普通卸载保留用户数据。

## 授权

项目源码和人工编写的示例词库：GPL-3.0-or-later。Fcitx5、LibIME、OpenCC、SQLite、Python/Tk 等由发行版安装，遵循各自许可证；基础词库与模型使用发行版的 LibIME 数据包。依赖与来源见 [后端决策](docs/backend-decision.md)。
