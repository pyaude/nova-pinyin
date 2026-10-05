# 验证记录

日期：2026-10-05。当前交付范围为 v0.2。

构建环境：隔离 Ubuntu 24.04 amd64，GCC 13.3、CMake 3.28、Fcitx5 5.1.7、LibIME 1.1.5、OpenCC 1.1.7、SQLite 3.45.1、GoogleTest 1.14。

开发容器宿主为 Ubuntu 20.04，安装包使用 Ubuntu 24.04 的编译器、库和包数据库构建，未混用宿主运行库。

## 已完成

- 23 项核心/后端测试通过：全拼、句子、逐段选择撤销、光标、分隔符/v、过期候选、跨会话修订号、三种双拼、模糊音、纠错、繁体、emoji、专业词条、重排映射、词库校验、事务替换、学习持久化、导出/清空、异步刷新、损坏数据库保留和清空代际检查。
- Fcitx5 事件集成测试通过：候选 UI、Space 提交、Esc、Enter 原文、Shift 切换、快捷键透传及密码/敏感能力提示。测试初始化 Fcitx5 Instance 后使用真实 Core API 和模拟客户端；不替代实际工具包测试。
- libpinyin 2.8.1 技术探针：`nihao` 解析长度 5，126 个候选中包含“你好”，一次测量约 2.6 ms。仅为基础能力检查，不能据此比较两个后端的完整质量。
- 热路径基准：Intel Core i7-9750H，RelWithDebInfo，30 轮词组/短句的逐键输入、候选构建和 preedit，1470 样本；一次测得 P50 1.02 ms、P95 14.45 ms、P99 16.60 ms、最大 19.09 ms；后端初始化约 179 ms，峰值 RSS 44312 KiB。结果不含工具包传输和屏幕绘制。
- 安装包安装后，在独立 D-Bus/Xvfb 会话启动真实 Fcitx5，通过 GTK3 Entry 和 Qt5 LineEdit 分别键入 `nihao`、Space，两个应用实际收到“你好”。
- 图形管理器构建窗口、保存双拼设置、导入两个示例词库及禁用词库通过。
- `.deb` 原生安装、卸载、重装通过；卸载保留测试用户数据库，重装后词库仍可列出。运行库依赖由 `dpkg-shlibdeps` 从 Ubuntu 24.04 包数据库生成。
- AddressSanitizer + UndefinedBehaviorSanitizer：22 项在原生 chroot 通过；另两项异常路径测试因 chroot 缺少 `/proc` 导致 ASan 无法识别线程栈，在宿主通过 Ubuntu 24.04 动态加载器及运行库复核通过。没有修改生产代码以规避检查；本次 LeakSanitizer 因该环境限制关闭，不能据此宣称无内存泄漏。

## 复现

常规构建、24 项测试和打包运行 `./scripts/build-deb.sh`。
图形测试需额外安装 `libgtk-3-dev qtbase5-dev xvfb xauth xdotool dbus-x11`，安装生成的包，然后：

```bash
cmake -S . -B build -DNOVA_GUI_SMOKE=ON
cmake --build build --parallel 2
NOVA_ISOLATED_GUI_TEST=1 dbus-run-session -- bash tests/gui-smoke.sh
NOVA_ISOLATED_GUI_TEST=1 XDG_DATA_HOME="$(mktemp -d)" \
  XDG_CONFIG_HOME="$(mktemp -d)" xvfb-run -a python3 tests/manager-smoke.py
```

图形脚本使用私有显示器 `:187`；如该显示器已被占用，请调整脚本，勿在已有用户桌面执行。GitHub Ubuntu 24.04 CI 已在提交 `6f827cc` 和 `4922f83` 通过常规测试及 AddressSanitizer/UndefinedBehaviorSanitizer，且启用了 LeakSanitizer；上述本地 chroot 限制不适用于 GitHub runner。CI 配置保留包产物上传；标签发布流程增加安装包的 GTK3/Qt5 与管理器复核，通过后创建 Pre-release。

## 未验证的桌面组合

GNOME 原生 Wayland、桌面搜索框/Kimpanel、多屏与缩放、Firefox Snap、Chromium/Electron、LibreOffice、GTK4/Qt6、Ubuntu 26.04 的独立包均需要实际目标环境继续验收。当前不能宣称所有桌面组合均通过。

v0.2 提供有限纠错、小型 emoji 表与示例领域词表，未包含 AI、云服务、项目索引和编辑器补全。安装指南说明如何恢复原输入框架。
