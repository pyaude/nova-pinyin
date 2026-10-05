# 验证记录

日期：2026-10-05。当前开发范围为 v0.3；保留 v0.2 的验证记录。

构建环境：隔离 Ubuntu 24.04 amd64，GCC 13.3、CMake 3.28、Fcitx5 5.1.7、LibIME 1.1.5、OpenCC 1.1.7、SQLite 3.45.1、GoogleTest 1.14。

开发容器宿主为 Ubuntu 20.04，安装包使用 Ubuntu 24.04 的编译器、库和包数据库构建，未混用宿主运行库。

## v0.3 验证

- 34 项 CTest 条目通过：32 项核心测试、Fcitx5 事件集成测试和项目索引测试（包含 4 个 Python 安全/边界场景）。
- 新测试包含显式触发和模式内空格、项目频次排序、大小写与编辑、过期候选拒绝、上下文 Unicode 边界/清除/冻结、真实静态语言模型排序变化、应用提示覆盖，以及 schema 1 迁移、原子导入失败保留、清空后拒绝过期索引。
- 索引验证覆盖嵌套 `.gitignore` 与否定规则、被忽略的已跟踪文件、`.novapinyinignore`、凭证/隐藏文件排除、注释与字符串剔除、二进制/超大文件跳过、目录符号链接及读取时父目录符号链接拒绝。
- Fcitx5 事件测试验证新能力默认关闭、显式命令及已加载项目标识符补全、失焦/Esc 后的旧候选无效、敏感/隐私模式禁用、选择 Enter 被消费、其他快捷键透传及应用标点策略覆盖。
- 安装 v0.3 包升级 v0.2 后，原领域词库可继续列出。独立 D-Bus/Xvfb 会话中 GTK3、Qt5 实际收到“你好”及显式补全的 `git checkout`；选择 Enter 未触发应用的激活信号，模式退出后额外 Enter 才触发。
- 管理器图形验证通过：保存新设置、后台索引源码、列出项目、保存活动项目选择；未阻塞窗口事件循环。
- 相同 i7-9750H 上，1470 样本逐键基准：关闭上下文 P95 14.69 ms，开启上下文 P95 14.61 ms；这是各一次测量，不能据此宣称上下文使输入更快。峰值 RSS 约 44 MiB。20000 标识符补全，300 样本 P95 0.71 ms、最大 1.04 ms；模式初始化最大 2.01 ms。全部不含屏幕绘制。
- v0.3 sanitizer 与公开发布的最终验证由原生 Ubuntu 24.04 GitHub CI 完成，状态以提交的 Actions 检查和发布流水线为准；本地 chroot 的 `/proc` 限制仍存在。

## v0.2 已完成

- 23 项核心/后端测试通过：全拼、句子、逐段选择撤销、光标、分隔符/v、过期候选、跨会话修订号、三种双拼、模糊音、纠错、繁体、emoji、专业词条、重排映射、词库校验、事务替换、学习持久化、导出/清空、异步刷新、损坏数据库保留和清空代际检查。
- Fcitx5 事件集成测试通过：候选 UI、Space 提交、Esc、Enter 原文、Shift 切换、快捷键透传及密码/敏感能力提示。测试初始化 Fcitx5 Instance 后使用真实 Core API 和模拟客户端；不替代实际工具包测试。
- libpinyin 2.8.1 技术探针：`nihao` 解析长度 5，126 个候选中包含“你好”，一次测量约 2.6 ms。仅为基础能力检查，不能据此比较两个后端的完整质量。
- 热路径基准：Intel Core i7-9750H，RelWithDebInfo，30 轮词组/短句的逐键输入、候选构建和 preedit，1470 样本；一次测得 P50 1.02 ms、P95 14.45 ms、P99 16.60 ms、最大 19.09 ms；后端初始化约 179 ms，峰值 RSS 44312 KiB。结果不含工具包传输和屏幕绘制。
- 安装包安装后，在独立 D-Bus/Xvfb 会话启动真实 Fcitx5，通过 GTK3 Entry 和 Qt5 LineEdit 分别键入 `nihao`、Space，两个应用实际收到“你好”。
- 图形管理器构建窗口、保存双拼设置、导入两个示例词库及禁用词库通过。
- `.deb` 原生安装、卸载、重装通过；卸载保留测试用户数据库，重装后词库仍可列出。运行库依赖由 `dpkg-shlibdeps` 从 Ubuntu 24.04 包数据库生成。
- AddressSanitizer + UndefinedBehaviorSanitizer：22 项在原生 chroot 通过；另两项异常路径测试因 chroot 缺少 `/proc` 导致 ASan 无法识别线程栈，在宿主通过 Ubuntu 24.04 动态加载器及运行库复核通过。没有修改生产代码以规避检查；本次 LeakSanitizer 因该环境限制关闭，不能据此宣称无内存泄漏。

## 复现

常规构建、34 项测试和打包运行 `./scripts/build-deb.sh`。索引测试与运行时 Git 忽略规则需要 `git`（安装包已声明依赖）。
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

v0.3 提供有限上下文排序与源码标识符/命令补全，未包含 AI、云服务、编辑器语义集成或自动项目发现。新功能默认关闭；使用边界、数据迁移与回退方法见 [高级输入](advanced-input.md)。
