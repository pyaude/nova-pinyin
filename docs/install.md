# Ubuntu 安装与恢复

发布包面向 Ubuntu 24.04 amd64；Ubuntu 26.04 应从源码或独立 CI 包安装，不复用未经验证的二进制。

1. 从 [v0.3.0 预发布](https://github.com/pyaude/nova-pinyin/releases/tag/v0.3.0) 下载 `novapinyin_0.3.0-1_amd64.deb` 和 `SHA256SUMS` 到同一目录，先运行 `sha256sum --check SHA256SUMS`，通过后运行 `sudo apt install ./novapinyin_0.3.0-1_amd64.deb`。依赖由系统仓库提供，首次安装依赖可能需要联网；输入本身离线。
2. 运行 `im-config`，选择 Fcitx5。记录此前使用的框架，按提示注销并重新登录。
3. 打开“Fcitx 5 配置”，点击添加，取消“仅显示当前语言”筛选，添加“NovaPinyin 拼音”。按框架设置中的快捷键（通常 Ctrl+Space）启用。
4. 输入 `nihao`，用 Space 或数字选词。单独 Shift 切换中英文。Enter 提交组合原文，下一次 Enter 才交给应用。
5. 在应用菜单打开“NovaPinyin设置”，设置双拼、模糊音、繁体等，或安装附带的示例词库。

配置也可从 Fcitx5 的输入法设置打开。双拼方案：自然码、小鹤、微软。中文模式支持常用标点；终端默认半角。表情候选包括 `xiaolian`、`zan`、`aixin`、`qingzhu` 等。

v0.3 新增的上下文与显式补全默认关闭。启用开发者补全后，按 `Ctrl+Alt+Space` 进入，Space 输入空格，Tab/Enter 只提交选中的文字，Esc 或失焦取消。项目需用户指定目录并显式索引。升级安装后通过 Fcitx5 菜单重新启动，或注销重登录，以加载新插件。配置、忽略规则及 v0.2 升级/回退说明见 [高级输入](advanced-input.md)。

当前源码的设置入口统一为“NovaPinyin设置”（命令 `novapinyin-manager`）。勾选“双拼”并选择自然码、小鹤或微软后保存；“候选配色”可选择浅色或深色。拼音候选用 **↓ 下一页、↑ 上一页**，也保留 PageDown/PageUp；空格和数字选词。没有拼音组合时，方向键交给应用；开发者补全模式的上下键仍选择候选。

在“本地词库 → 安装内置词库…”选择 Rime 常用词精选（20,000 条）或雾凇社区补充词（162 条），点击“安装选中词库”。这一步离线完成，默认启用，可在列表禁用或移除。来源、固定版本和许可见 [词库来源](../data/dictionaries/SOURCES.md)。上述新增界面和行为包含于本地 `.4` 构建，原 v0.3.0 GitHub Release 尚不包含这些优化。

## Ubuntu 20.04 专用包

Ubuntu 20.04 amd64 使用单独的试用包 `novapinyin_0.3.0-1~ubuntu20.04.5_amd64.deb`，不要安装上述 Ubuntu 24.04 包。本次专用构建产物位于 `dist/ubuntu20.04/`，尚未上传 GitHub Release。

将专用 `.deb` 和该目录的 `SHA256SUMS` 保存到同一目录，执行：

```bash
sha256sum --check SHA256SUMS
sudo apt install --no-install-recommends ./novapinyin_0.3.0-1~ubuntu20.04.5_amd64.deb
```

安装完成后，注销并重新登录 **X11/Xorg 会话**，或重启一次。`.5` 会在首次登录自动备份原输入法选择、启用 NovaPinyin、配置 GTK/Qt/XIM 环境并启动专用框架；无需手动运行 `im-config` 或添加输入法。已运行应用的环境不能由安装程序追溯修改，因此仍需这一次重新登录。首次启用已包含键盘和 NovaPinyin，按 Ctrl+Space 切换，再输入 `nihao` 用 Space 选词。应用菜单中的“NovaPinyin 输入法配置”用于调整输入法列表，“NovaPinyin设置”用于输入设置、词库和项目管理；也可运行 `novapinyin-fcitx5-configtool` 和 `novapinyin-manager`。

首个专用包 `0.3.0-1~ubuntu20.04.1` 的入口编号误用 `90`，Ubuntu 20.04 的 `im-config` 不加载该编号：即使专用进程已手动启动，登录环境仍可能缺少输入模块变量，`im-config -m` 的第二行显示 `bogus`。`.2` 至 `.4` 需要运行 `im-config -n novapinyin` 后注销重登录；`.5` 在下次 X11 登录自动完成该选择，第二行应为 `novapinyin`。Ubuntu 系统“输入源”中的中文选择不能代替输入框架配置。修正版入口编号为 `77`，自动设置仍通过系统 im-config 的标准选择和启动流程完成。

登录后可运行以下命令检查会话环境；本包的 GTK/Qt 模块名均为 `fcitx5`：

```bash
im-config -m
printf 'session=%s\nGTK=%s\nQt=%s\nXIM=%s\n' "$XDG_SESSION_TYPE" "$GTK_IM_MODULE" "$QT_IM_MODULE" "$XMODIFIERS"
```

预期会话为 `x11`，GTK 和 Qt 为 `fcitx5`，XIM 为 `@im=fcitx`。先在系统自带 GTK3 文本编辑器中输入 `nihao` 并用空格选词；若只有特定应用失败，再检查该应用的输入模块或沙箱环境。

`.3` 已修正专用包的浅色/深色主题生成问题：旧构建工具会合并空 `Image=` 和后续 `Color=`，使选中候选显示为白底白字。升级后注销重登录，加载修正的主题和插件；拼音候选改为横向排列。用户已有的自定义主题和配置不覆盖，实际微信 Linux 客户端效果仍需在用户机器上复验。

`.4` 在此基础上统一设置入口，扩大候选间距，默认使用高对比蓝色选中项，增加浅色／深色配色选择、方向键翻页和可选内置词库。升级后注销重登录以加载插件及主题；已有词库和设置保留。

包内置 Fcitx5 5.1.7、LibIME 1.1.5、Qt 绑定和配置工具 5.0.17，位于 `/usr/lib/novapinyin/focal/`；GTK3/Qt5 输入模块及其他系统库由 Ubuntu 20.04 官方仓库安装，无需添加 PPA 或替换系统 glibc/libstdc++。首次安装依赖可能需要联网，输入运行时离线。

个人数据库仍遵循 `XDG_DATA_HOME`，默认 `~/.local/share/novapinyin/user.db`。专用框架配置为 `~/.config/novapinyin/fcitx5/`（遵循 `XDG_CONFIG_HOME`），原 `~/.config/fcitx5/profile` 保留。首次运行将已有 NovaPinyin 设置复制到专用目录，随后保留专用设置，不反复覆盖；未知选项保留。

`.5` 的自动启用记录及原选择备份位于 `$XDG_CONFIG_HOME/novapinyin/session-setup.json`（默认 `~/.config/novapinyin/`），权限为仅当前用户可读写；家目录中的 `.xinputrc.novapinyin-setup` 用于在卸载时定位记录。后续通过 im-config 选择其他框架时，不会在每次登录重新改回 NovaPinyin。手工维护的 `.xinputrc`、符号链接或非本人拥有的配置会保留；这类自定义环境需按自己的会话配置启用。

需要保留安装包但恢复原框架时，可运行 `novapinyin-session-setup --restore`，然后注销重登录；自动启用会停止。卸载 `.5` 时，若输入法选择仍是该包写入的版本，会恢复备份；首次安装前没有个人选择，或原来已选择旧版 NovaPinyin 时，回到系统默认。用户安装后自行改过的选择保留。普通卸载保留个人词条、词库、项目索引和输入设置，完成后仍需注销重登录。

此包关闭原生 Wayland 支持，当前验证为 Ubuntu 20.04 Docker 下的 Xvfb、GTK3、Qt5 和管理器，尚不能代表完整 GNOME 桌面、Snap/Electron 或其他架构。校验后的完整源码归档及单独的 `SOURCE_SHA256SUMS` 同时提供；重新构建步骤见 `packaging/focal/BUILD.txt`。

## GNOME / Wayland

每个应用的输入接口不同，不能通过一个全局环境变量保证所有应用兼容。先在 GTK 文本框验证，再检查应用实际使用 Wayland 还是 XWayland。Qt 应用需要对应 Fcitx5 输入模块；原生 Wayland Chromium/Electron 的参数按具体版本确认。

GNOME 桌面搜索框或候选窗位置可能需要 Kimpanel 扩展。不要直接覆盖已有桌面环境变量或扩展配置。遵循 [Fcitx5 官方 Wayland 文档](https://fcitx-im.org/wiki/Using_Fcitx_5_on_Wayland/en)，以实际系统测试为准。

本次容器不能代表 GNOME Wayland 桌面；当前验证范围及待验证组合见 `validation.md`。Snap/Flatpak 需沙箱内输入模块支持，宿主安装并不能保证兼容。

## 故障与恢复

- 无输入法：运行 `fcitx5-diagnose`，确认运行的是 Fcitx5、输入法已添加、注销后环境生效。
- 仅某个应用失败：检查工具包输入模块、应用后端和沙箱来源；不要反复全局改环境变量。
- 学习数据失败：基础输入仍可用。检查 `~/.local/share/novapinyin/` 权限和磁盘空间；保留 `user.db` 备份，不直接删除故障文件。
- 设置异常：NovaPinyin设置选择“恢复默认设置”；这不会清空个人词条。
- 卸载：`sudo apt remove novapinyin`。Ubuntu 20.04 `.5` 自动恢复由它管理的原输入法选择；24.04 包及早期 20.04 包需在 `im-config` 选择原来的输入框架。注销重登录。卸载保留个人数据；需要清空时先在“NovaPinyin设置”清空或导出备份，再自行移除个人数据目录。

个人数据遵循 `XDG_DATA_HOME`，默认 `~/.local/share/novapinyin/user.db`；配置默认 `~/.config/fcitx5/conf/novapinyin.conf`。隐私模式和密码/敏感提示关闭学习与个人词条；客户端不总能识别敏感字段，输入敏感内容前可手动启用隐私模式。
