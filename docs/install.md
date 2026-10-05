# Ubuntu 安装与恢复

发布包面向 Ubuntu 24.04 amd64；Ubuntu 26.04 应从源码或独立 CI 包安装，不复用未经验证的二进制。

1. 下载包后运行 `sudo apt install ./novapinyin_0.3.0-1_amd64.deb`。依赖由系统仓库提供，首次安装依赖可能需要联网；输入本身离线。
2. 运行 `im-config`，选择 Fcitx5。记录此前使用的框架，按提示注销并重新登录。
3. 打开“Fcitx 5 配置”，点击添加，取消“仅显示当前语言”筛选，添加“NovaPinyin 拼音”。按框架设置中的快捷键（通常 Ctrl+Space）启用。
4. 输入 `nihao`，用 Space 或数字选词。单独 Shift 切换中英文。Enter 提交组合原文，下一次 Enter 才交给应用。
5. 在应用菜单打开“NovaPinyin 词库管理”，设置双拼、模糊音、繁体等，或安装附带的示例词库。

配置也可从 Fcitx5 的输入法设置打开。双拼方案：自然码、小鹤、微软。中文模式支持常用标点；终端默认半角。表情候选包括 `xiaolian`、`zan`、`aixin`、`qingzhu` 等。

v0.3 新增的上下文与显式补全默认关闭。启用开发者补全后，按 `Ctrl+Alt+Space` 进入，Space 输入空格，Tab/Enter 只提交选中的文字，Esc 或失焦取消。项目需用户指定目录并显式索引。升级安装后通过 Fcitx5 菜单重新启动，或注销重登录，以加载新插件。配置、忽略规则及 v0.2 升级/回退说明见 [高级输入](advanced-input.md)。

## GNOME / Wayland

每个应用的输入接口不同，不能通过一个全局环境变量保证所有应用兼容。先在 GTK 文本框验证，再检查应用实际使用 Wayland 还是 XWayland。Qt 应用需要对应 Fcitx5 输入模块；原生 Wayland Chromium/Electron 的参数按具体版本确认。

GNOME 桌面搜索框或候选窗位置可能需要 Kimpanel 扩展。不要直接覆盖已有桌面环境变量或扩展配置。遵循 [Fcitx5 官方 Wayland 文档](https://fcitx-im.org/wiki/Using_Fcitx_5_on_Wayland/en)，以实际系统测试为准。

本次容器不能代表 GNOME Wayland 桌面；当前验证范围及待验证组合见 `validation.md`。Snap/Flatpak 需沙箱内输入模块支持，宿主安装并不能保证兼容。

## 故障与恢复

- 无输入法：运行 `fcitx5-diagnose`，确认运行的是 Fcitx5、输入法已添加、注销后环境生效。
- 仅某个应用失败：检查工具包输入模块、应用后端和沙箱来源；不要反复全局改环境变量。
- 学习数据失败：基础输入仍可用。检查 `~/.local/share/novapinyin/` 权限和磁盘空间；保留 `user.db` 备份，不直接删除故障文件。
- 设置异常：词库管理器选择“恢复默认设置”；这不会清空个人词条。
- 卸载：`sudo apt remove novapinyin`，再在 `im-config` 选择原来的输入框架，注销重登录。卸载保留个人数据；需要清空时先在词库管理器清空或导出备份，再自行移除个人数据目录。

个人数据遵循 `XDG_DATA_HOME`，默认 `~/.local/share/novapinyin/user.db`；配置默认 `~/.config/fcitx5/conf/novapinyin.conf`。隐私模式和密码/敏感提示关闭学习与个人词条；客户端不总能识别敏感字段，输入敏感内容前可手动启用隐私模式。
