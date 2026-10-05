# 验证记录

日期：2026-10-06。当前开发范围为 v0.3；保留 v0.2 的验证记录。

早期构建环境：隔离 Ubuntu 24.04 amd64，GCC 13.3、CMake 3.28、Fcitx5 5.1.7、LibIME 1.1.5、OpenCC 1.1.7、SQLite 3.45.1、GoogleTest 1.14。

早期开发容器宿主为 Ubuntu 20.04，安装包使用 Ubuntu 24.04 的编译器、库和包数据库构建，未混用宿主运行库。

## v0.3 稳定性收尾（2026-10-05 至 06）

本次在 Ubuntu 26.04.1 宿主上新建 Ubuntu 24.04.5 amd64 Docker 容器，使用容器内 GCC 13.3、CMake 3.28.3、Fcitx5 5.1.7、LibIME 1.1.5、OpenCC 1.1.7、SQLite 3.45.1、GoogleTest 1.14 构建。容器提供正常的 `/proc`，不再沿用此前 chroot 的内存检查限制。

- 常规构建与 35 项 CTest 全部通过：33 项核心测试、插件事件测试（2 个场景）与项目索引测试（14 个 Python 场景）。
- 独立 Debug 构建通过全部 35 项 AddressSanitizer / UndefinedBehaviorSanitizer 检查；`detect_leaks=1:halt_on_error=1` 与 `halt_on_error=1:print_stacktrace=1` 保持开启，无抑制规则。上下文静态模型修复已复测，新增长上下文、连续编辑及旧会话跨后端刷新检查。
- 插件测试改为等待项目候选实际可用，限时 20 秒，避免固定启动等待在 sanitizer 下抢跑。真实 Qt 检查发现普通学习刷新会取消活动补全；新增回归在修复前失败，修复后通过，补全前缀、可见候选和选择 Enter 的消费规则保持不变。显式数据代次变化仍使旧会话失效。
- 索引器在目录枚举过程中限制目录条目数与耗时，使用根目录描述符逐级打开目录，拒绝遍历期间替换的符号链接；覆盖读取失败、最后一个文件提取、Git 剩余预算、深度/文件/累计字节上限与失败后不导入部分索引。二进制和非 UTF-8 文件计入已读取量，所有尝试读取的源码文件计入文件预算。
- 安装本次构建包，在独立 D-Bus/Xvfb 中验证 GTK3、Qt5 实际收到“你好”和 `git checkout`，触发键退出不提交前缀，选择 Enter 不触发应用激活，额外 Enter 才交给应用。已安装管理器通过设置、示例词库、后台索引与活动项目选择检查。
- 管理器图形测试自动创建独立 D-Bus 会话，避免保存设置时的重载请求接触桌面进程，也避免 `fcitx5-remote` 在缺少会话总线时异常退出；原有 `xvfb-run` 调用仍可使用。
- 临时 XDG 目录下验证 v0.2 升级：个人词条及选择次数、领域词库保留；卸载/重装保留个人词条和项目索引；显式清空删除个人词条与项目索引，保留领域词库。
- i7-9750H、RelWithDebInfo，在本项目其他验证任务结束后各运行一次 1470 样本逐键基准：关闭上下文 P95 17.07 ms、P99 22.35 ms、最大 43.15 ms；开启上下文 P95 16.62 ms、P99 21.01 ms、最大 30.68 ms；峰值 RSS 约 44 MiB。当前单次测量未达到建议的 P95 < 15 ms，不能据此认定性能验收完成或上下文更快。20000 标识符、300 样本补全 P95 2.03 ms、最大 2.36 ms，模式初始化最大 2.82 ms。容器共享宿主负载，数据不含传输与屏幕绘制。
- 发布工作流已加入独立 sanitizer 步骤，检查失败不会进入发布阶段。以上为提交前本地工作区的验收结果；对应源码提交的远程 CI 状态需另行确认，不能用本地检查代替。本轮未创建发布标签或 Release；此前提交的远程失败记录仍保留。

本次安装包与可直接校验的 `SHA256SUMS` 位于 `dist/v0.3-validation/`；原 `dist/` 包不作为本次产物。构建目录为 `build-noble`、`build-asan-noble`。原生 GNOME/Wayland 及下文所列桌面矩阵仍未验收，本轮未引入 v0.4 AI、云服务或编辑器集成。

## v0.3 初次验证记录

- 34 项 CTest 条目通过：32 项核心测试、Fcitx5 事件集成测试和项目索引测试（包含 5 个 Python 安全/边界场景）。
- 新测试包含显式触发和模式内空格、项目频次排序、大小写与编辑、过期候选拒绝、上下文 Unicode 边界/清除/冻结、真实静态语言模型排序变化、应用提示覆盖，以及 schema 1 迁移、原子导入失败保留、清空后拒绝过期索引。
- 索引验证覆盖嵌套 `.gitignore` 与否定规则、被忽略的已跟踪文件、`.novapinyinignore`、凭证/隐藏文件排除、注释与字符串剔除、二进制/超大文件跳过、目录符号链接及读取时父目录符号链接拒绝。
- Fcitx5 事件测试验证新能力默认关闭、显式命令及已加载项目标识符补全、失焦/Esc 后的旧候选无效、敏感/隐私模式禁用、选择 Enter 被消费、其他快捷键透传及应用标点策略覆盖。
- 安装 v0.3 包升级 v0.2 后，原领域词库可继续列出。独立 D-Bus/Xvfb 会话中 GTK3、Qt5 实际收到“你好”及显式补全的 `git checkout`；选择 Enter 未触发应用的激活信号，模式退出后额外 Enter 才触发。
- 管理器图形验证通过：保存新设置、后台索引源码、列出项目、保存活动项目选择；未阻塞窗口事件循环。
- 卸载/重装 v0.3 保留已有项目索引，显式清空学习数据后项目列表为空。
- 相同 i7-9750H 上，1470 样本逐键基准：关闭上下文 P95 14.69 ms，开启上下文 P95 14.61 ms；这是各一次测量，不能据此宣称上下文使输入更快。峰值 RSS 约 44 MiB。20000 标识符补全，300 样本 P95 0.71 ms、最大 1.04 ms；模式初始化最大 2.01 ms。全部不含屏幕绘制。
- 当时提交 `e33e7e5` 的 GitHub 常规构建/测试通过，sanitizer 步骤失败，未发布 v0.3。源码审查发现上下文评分把临时 WordNode 传入会在状态中保留节点指针的 UserLanguageModel；当时已改为共享原模型文件的静态 LanguageModel，但执行环境随后无法启动命令进程，修改尚未复测和提交。本次本地复测见上文；该提交的远程失败记录仍保留。CI 记录：[Actions](https://github.com/pyaude/nova-pinyin/actions/runs/37315288999)。

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

常规构建、35 项测试和打包运行 `./scripts/build-deb.sh`。索引测试与运行时 Git 忽略规则需要 `git`（安装包已声明依赖）。使用独立目录可运行 `NOVA_BUILD_DIR=build-noble NOVA_PACKAGE_DIR=dist/v0.3-validation ./scripts/build-deb.sh`；图形脚本同样支持 `NOVA_BUILD_DIR`。
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
