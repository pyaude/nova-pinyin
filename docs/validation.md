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
- 发布工作流已加入独立 sanitizer 步骤，检查失败不会进入发布阶段。源码修复提交 `6b813b6` 的 GitHub Ubuntu 24.04 构建、测试、打包和 AddressSanitizer/UndefinedBehaviorSanitizer（含泄漏检测）已全部通过：[构建 CI](https://github.com/pyaude/nova-pinyin/actions/runs/37340299013)。此前提交的远程失败记录仍保留；它不代表本次修复提交的检查结果。

稳定性收尾的本地安装包与 `SHA256SUMS` 位于 `dist/v0.3-validation/`；v0.3.0 发布准备重新构建的本地包使用 `dist/v0.3-release/`。构建目录为 `build-noble`、`build-asan-noble`。正式上传的预发布资产由标签工作流在 Ubuntu 24.04 重建，以 [v0.3.0 Release](https://github.com/pyaude/nova-pinyin/releases/tag/v0.3.0) 的 `.deb` 和 `SHA256SUMS` 为准，不能用历史本地包替代。发布准备提交需先通过对应构建 CI，再创建标签；标签工作流再次执行内存、安装和图形检查，通过后才发布。原生 GNOME/Wayland 及下文所列桌面矩阵仍未验收，本轮未引入 v0.4 AI、云服务或编辑器集成。

## v1.0.0 正式发布准备（2026-10-06）

按用户要求发布正式版，保留当前功能范围，分别构建 Ubuntu 20.04 / 24.04 amd64 包。上游版本与 Debian 基础版本同步为 1.0.0 / 1.0.0-1；文件名和包内版本分别含 `~ubuntu20.04.1`、`~ubuntu24.04.1`，构建脚本校验目标系统，避免混用宿主 ABI。Ubuntu 20.04 保留 v0.3 `.5` 的自动 X11 登录设置；Ubuntu 24.04 继续使用系统框架并按安装说明首次选择。

- 在独立 Ubuntu 20.04.6 / 24.04.5 容器中重新构建 v1.0 包，两边常规 CTest 均 35/35 通过；各自独立 ASan/UBSan 35/35 通过，泄漏检测保持开启。
- 分别实际 apt 升级 v0.3 `.5` 专用包及 v0.3 系统包到 v1.0，验证个人词条和选择次数、领域词库、项目索引、双拼及未知设置保留；卸载/重装后再验证保留。安装和卸载步骤前后比较数据库与配置字节，CLI 导出及列表保持一致。临时用户和 XDG 目录与实际桌面隔离。
- 两边安装后 GTK3/Qt5 均实际提交“你好”及显式补全，选择 Enter 被消费；管理器双拼/主题保存、词库安装/启停、后台项目索引及选择通过。两套命名主题的 GTK/Qt 四种组合均验证可见蓝底白字、横向间距及 ↓/↑ 翻页；20.04 的两套专用默认主题另测四种组合。
- Ubuntu 20.04 新用户自动登录/切换及恢复 9 项全部通过；旧 IBus 环境下完整 Xsession/im-launch 后，Ctrl+Space 切换并提交中文。原生 im-config 两阶段、专用配置和 Qt 配置工具复验通过。该测试改为从源码目录和版本模板定位包/客户端，支持 GitHub 的工作区路径。
- 发布流程增加 Ubuntu 20.04 原生容器检查，并保留 Ubuntu 24.04 常规及 sanitizer 检查。标签发布依赖两套系统全部通过，再汇总两份安装包、20.04 完整匹配源码和 SHA256SUMS，以正式版和 latest 发布。发布准备提交仍须先通过远程构建 CI，随后才创建 v1.0.0 标签；最终状态以 Release 为准。

本地安装包、日志位于 `dist/v1.0.0/`（20.04 原始输出仍在 `dist/ubuntu20.04/`），正式交付以 [v1.0.0 Release](https://github.com/pyaude/nova-pinyin/releases/tag/v1.0.0) 的资产为准。当前验证不覆盖完整 GNOME 登录桌面、原生 Wayland、微信 Linux、Edge、LibreOffice、Snap/Flatpak、多屏缩放和 GTK4/Qt6。未构建或声明支持 Ubuntu 22.04 / 26.04、arm64。正式版标记不扩大上述实际验证范围。

## v0.3.0 发布准备复验（2026-10-06）

在上述 Ubuntu 24.04.5 amd64 容器中重新运行打包脚本，35 项 CTest 全部通过。安装新包后，独立 D-Bus/Xvfb 下 GTK3、Qt5 中文和显式补全提交、选择 Enter 消费规则及管理器图形检查通过。使用临时 XDG 目录再次验证 v0.2 升级的个人词条、选择次数与领域词库保留，卸载/重装的个人词条与项目索引保留，以及显式清空后个人词条和项目索引移除、领域词库保留。源码与已通过内存检查的 `6b813b6` 一致；发布准备仅调整发布和安装文档，仍须通过对应提交的远程 CI 后创建标签。

## Ubuntu 20.04 amd64 专用试用包（2026-10-06）

按用户要求在独立 Ubuntu 20.04 Docker 环境构建，未使用宿主 Ubuntu 26.04 或 Ubuntu 24.04 的二进制库。编译器为 GCC 10.5，glibc 2.31、SQLite 3.31.1、OpenCC 1.0.5、Python 3.8.10、GoogleTest 1.10、Qt 5.12.8；CMake 3.28.3 仅为构建工具。

- Ubuntu 20.04 官方 Fcitx5/LibIME 为早期开发版，无法满足引擎。本包从固定、校验后的源码编译 Fcitx5 5.1.7、LibIME 1.1.5、xcb-imdkit 1.0.8、Qt 绑定和配置工具 5.0.17，携带匹配的词库/模型，安装到 `/usr/lib/novapinyin/focal/`。系统 GTK3/Qt5 前端和其他库仍由 Focal 官方仓库提供。
- 兼容性修复：显式包含旧 OpenCC 的导出定义，词库路径可配置；SQLite 学习时间改用等价的 `CAST(strftime('%s','now') AS INTEGER)`，解决 3.31 不支持 `unixepoch()` 导致的学习失败，保持 schema 与事务不变。管理器支持 `FCITX_CONFIG_HOME`。
- 35 项常规 CTest 和独立 ASan/UBSan（含 `detect_leaks=1`）全部通过，包含 14 个 Python 3.8 项目索引场景。Ubuntu 24.04 的 35 项常规回归测试也通过；本轮尚无 Ubuntu 20.04 远程 CI 记录。
- 在另一个未装开发库的 Ubuntu 20.04 容器中，通过 `apt install --no-install-recommends` 安装实际 `.deb`，依赖正常解析，无未找到的运行库；生成依赖要求 `libc6 >= 2.30`、`libstdc++6 >= 9`，没有替换系统基础库。
- 安装后的独立 D-Bus/Xvfb 检查通过：GTK3、Qt5 实际收到“你好”和显式补全，取消不提交前缀，选择 Enter 不触发应用激活；管理器设置、词库、后台索引和项目选择通过。专用启动器、Qt 配置工具窗口、旧 Fcitx5 profile 保留、已有引擎设置一次复制及未知选项/专用设置重启后保留已验证。
- 临时 XDG 目录验证 schema 1 自动迁移、个人词条及次数保留；卸载/重装保留词条、领域词库、项目索引和专用设置；显式清空移除个人学习与项目索引，领域词库仍保留。

产物位于 `dist/ubuntu20.04/`：`novapinyin_0.3.0-1~ubuntu20.04.1_amd64.deb`、二进制 `SHA256SUMS`、完整匹配源码归档和 `SOURCE_SHA256SUMS`；本次为本地专用构建，未创建新发布标签或上传 Release。可重复构建入口为 `scripts/build-focal-deb.sh`，依赖准备见 `packaging/focal/BUILD.txt`。框架配置与系统 Fcitx5 分开，个人数据库继续使用原 XDG 位置，安装/卸载不写用户配置。原生 Wayland 在该构建中关闭；Xvfb 的通过不能代表 GNOME 原生桌面、Snap/Electron、GTK4/Qt6 或其他架构已验收。本轮没有 Ubuntu 20.04 的性能验收结论。

### Ubuntu 20.04 登录入口修正（同日，`.2`）

用户反馈 `.1` 已启动专用进程但不能输入中文，`im-config -m` 第二行为 `bogus`。检查 Focal im-config 0.44-1ubuntu1.3 源码确认 `active_im` 和 `run_im` 只识别编号 `00–89`；初版误用 `90_novapinyin`，配置可写入却不能在登录时加载。此前图形测试手动设置环境变量并启动进程，未覆盖此登录入口，不能据此声明 `.1` 登录启用成功。

- 改为 `77_novapinyin`，`package_auto` 返回失败，仅显式选择后启用，保持原自动输入框架规则；版本为 `0.3.0-1~ubuntu20.04.2`。
- 新增 `tests/focal-imconfig-test.py`，在旧实际安装包上因入口缺失而失败；升级实际 `.2` 后通过菜单发现、原生 `run_im` 第一阶段环境变量校验、第二阶段启动及 NovaPinyin 插件可用检查。测试使用临时 XDG 目录及独立 D-Bus/Xvfb，不修改真实用户配置。
- 隔离测试用户运行 `im-config -n novapinyin`，随后 `im-config -m` 第二行确认为 `novapinyin`；升级移除旧编号入口。
- `.2` 打包运行的 35 项常规 CTest 全部通过；安装后 GTK3/Qt5 中文及补全/Enter 处理、专用配置与 Qt 配置工具、管理器均复验通过。引擎源码未改动，内存检查沿用上节同一引擎源码的结果，本轮未重新运行 sanitizer。

当前 `SHA256SUMS` 和 `SOURCE_SHA256SUMS` 对应 `.2`，完整匹配源码同时提供，未上传 GitHub Release。仍仅验证隔离环境下的 X11 程序及原生 im-config 加载流程，不能代表所有真实 GNOME 会话或应用已通过。

### Ubuntu 20.04 候选显示修正（同日，`.3`）

用户在微信 Linux 原生版反馈首个选中候选为空白、候选列表纵向排列。独立 Xvfb/GTK3 窗口复现同样的空白首候选；并非词条缺失，而是 Focal gettext 0.19.8 的 `msgfmt --desktop` 把上游主题模板的空 `Image=` 与下一行合并为 `Image=Color=#808080`，导致选中背景丢失、白色文字绘制在白色背景上。浅色、深色主题的候选及菜单背景均受影响。

- 构建时从两份上游主题模板移除空 `Image=`（框架默认值仍为空），保留随后颜色行；翻译生成后检查候选/菜单背景和高亮配置，异常时停止打包。固定源码归档及其下载校验值未改动，处理步骤在构建脚本中可重复执行。
- 拼音候选改为 `Horizontal`；补全列表保持适合长标识符的原有纵向排列。新增事件检查确认布局提示及首候选实际文字，选择映射和按键规则保持一致。
- 新增 `tests/candidate-ui-smoke.py`，独立 D-Bus/Xvfb、临时 XDG 目录，读取真实 X11 候选窗像素并保存 PNG。旧 `.2` 安装包因高亮背景缺失而失败；`.3` 实际安装包在 GTK3/Qt5、浅色/深色四种组合下验证选中候选背景与文字可见、横向窗口（324×82）及提交“好”均通过。测试字体为 Focal `fonts-noto-cjk`，不修改运行时字体配置或真实用户数据。
- Ubuntu 20.04 的 35 项常规 CTest、独立 ASan/UBSan（`detect_leaks=1`）全部通过；安装后原生 im-config 两阶段、专用配置/Qt 配置工具、管理器及 GTK/Qt 中文和补全/Enter 行为复验通过。
- Ubuntu 24.04 初次并行回归时 `Storage.AsyncFlush` 的固定 2 秒初始化等待未满足，抛出“个人学习数据尚未就绪”；未修改测试等待或跳过用例。停止其他测试后，串行完整 35 项常规及 35 项 ASan/UBSan（含泄漏检查）均通过。初次失败日志保留，不能把它报告为通过。

该轮二进制及完整匹配源码为 `0.3.0-1~ubuntu20.04.3`，`dist/ubuntu20.04/` 校验文件对应 `.3`；实际渲染截图位于该目录的 `ui-smoke-v3/`。未安装或实测微信 Linux 客户端，用户升级后仍需确认实际微信效果；仍不声明所有 GNOME/Wayland 或应用组合通过。未上传新 GitHub Release。


### 设置、候选与随包词库优化（2026-10-06，`.4`）

- 桌面中文入口及窗口标题统一为“NovaPinyin设置”，保留自然码、小鹤、微软双拼选择，增加浅色／深色候选主题。主题保存保留其他根选项及未知配置节，不在安装时改写用户配置。
- 经典候选主题的左右文字边距从 5 增至 10，浅色使用淡灰底、深色使用深灰底，选中项均为蓝底白字。Focal 专用默认主题应用同一配色；用户自定义主题保留。拼音候选的 ↓/↑ 分别翻到下一页／上一页，保留 PageDown/PageUp；无组合时透传，补全模式上下键仍选择候选。
- 随包保留固定提交的 Rime 常用词源（Apache-2.0）及雾凇 `others.dict.yaml`（GPL-3.0）、完整许可与 SHA256。转换后分别 20,000 / 162 条，通过原生词库校验器；管理器选择安装、启用或禁用，运行时不联网。来源与转换范围见 `data/dictionaries/SOURCES.md`，主题图标来源及许可见 `data/themes/SOURCES.md`。
- Ubuntu 20.04 原生容器：常规 35 项 CTest 与独立 ASan/UBSan 35 项全部通过，`detect_leaks=1:halt_on_error=1`、`UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`。Ubuntu 24.04 原生容器也通过常规及相同内存检查各 35 项。
- Noble 首轮常规检查两个数据库断言失败，原因是旧测试以 PID/计数命名目录，容器重启后复用残留数据。两份 C++ 测试改用 `mkdtemp` 原子创建独立目录，未删除旧目录或降低断言；修正后两套系统完整检查通过。首轮失败日志保留在交付目录。
- 安装后的 Xvfb/私有 D-Bus 图形检查：GTK3/Qt5 × 两套命名主题及两套专用默认主题，共八种组合，蓝色选中背景与白色词条可见，横向窗口均为 422×96；原 `.3` 同环境为 324×82。↓ 翻页、↑ 返回首页、不误提交及随后空格提交“好”全部通过。测试按每个工具包／主题隔离输出文件，并等待客户端输入上下文启用；截图与框架日志保留。
- 已安装管理器通过双拼／小鹤方案保存、配色保存、未知选项保留、重开窗口读取主题、两套完整词库导入及词库启停、后台项目索引与活动项目选择。原生 im-config 两阶段、专用配置/Qt 配置工具、GTK/Qt 中文与补全/Enter 消费规则复验通过。设置页实机程序截图另保存，仅使用隔离测试数据。
- 实际 apt `.3` → `.4` 升级验证通过：升级前后个人条目及选择次数、领域词库、项目索引与配置文件保留，安装过程未改变测试数据文件字节；升级后 CLI 读取与导出一致。

该轮本地版本为 `0.3.0-1~ubuntu20.04.4`，产物、匹配源码及校验文件位于 `dist/ubuntu20.04/`，候选窗截图位于 `ui-smoke-v4/`。尚未提交、推送或上传 GitHub Release。本轮仍未安装或实测微信 Linux 原生客户端，Xvfb 的 GTK/Qt 检查不能代表实际微信、完整 GNOME/Wayland 或所有应用组合；专用包仍针对 X11。

### Ubuntu 20.04 自动启用（2026-10-06，`.5`）

- 按用户确认的安装体验增加一次性 X11 登录设置：安装完成后只需注销重登录或重启一次。以登录用户身份备份原 im-config 选择，自动启用 NovaPinyin，并在原生 `70im-config_launch` 之前修正继承的 IBus 环境；继续使用系统 im-launch 启动，未跳过环境一致性检查。安装时不强制退出桌面或猜测桌面用户名。
- 备份遵循 XDG_CONFIG_HOME，家目录保留定位链接；后续用户选择其他框架或显式恢复后不会反复改回。手工 `.xinputrc` 和符号链接保留。卸载仅恢复尚未被用户改动的受管选择，以原用户身份操作，保留个人数据库、词库、项目索引和设置。
- 新增安装后测试在隔离 Focal 容器创建并删除临时用户，共 9 项全部通过：全新用户完整 Xsession/im-launch、已有 IBus 备份和恢复、旧 NovaPinyin 选择、手工配置、符号链接、后续用户改动、安装中断恢复、root 跳过、实际 apt 重装/卸载/再安装及数据保留。测试覆盖带空格的 XDG_CONFIG_HOME。全新用户继承旧 IBus 变量时，GTK3/Qt5 客户端无需手动选引擎，实际 Ctrl+Space 切换并提交“你好”。
- 首轮完整登录测试发现插件首次按需加载时，构造函数内重载配置查询自身输入引擎，导致 Fcitx5 插件加载重入。已在构造完成后才执行会话失效处理；原失败日志保留。修复后完整登录输入及上述 9 项检查通过。
- 最终源码在 Ubuntu 20.04.6（GCC 10.5.0、glibc 2.31）和 Ubuntu 24.04.5（GCC 13.3.0、glibc 2.39）均通过 35 项常规 CTest、35 项独立 ASan/UBSan。内存检查保持 `detect_leaks=1:halt_on_error=1` 和 `UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`。
- 安装后复验通过：GTK3/Qt5 × 四套浅色/深色主题的八种候选组合、可见高亮字、横向间距及方向键翻页；管理器双拼/主题/词库/项目操作；原生 im-config 两阶段；专用配置及 Qt 配置工具；GTK/Qt 中文和显式补全、Enter 消费。实际 `.4` → `.5` apt 升级及 `.5` 重装/卸载/再安装通过。

该轮本地版本为 `0.3.0-1~ubuntu20.04.5`，安装包、匹配源码、校验文件、日志及验证摘要位于 `dist/ubuntu20.04/`，候选窗截图位于 `ui-smoke-v5/`。尚未提交、推送或上传 GitHub Release。自动启用仅包含在 20.04 专用包，目标仍是 X11；未在实际 GNOME 登录桌面或微信 Linux、Edge、LibreOffice 中运行本轮安装包。隔离 GTK/Qt 登录验证不能代替上述桌面和应用组合验收。

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
