# NovaPinyin 开发协作指南

本文件适用于整个仓库。用户当前指令优先于本指南；子目录如有更具体的 `AGENTS.md`，修改该目录时也应遵循。

## 项目目标与范围

- 项目名称为 **NovaPinyin**，GitHub 仓库为 `pyaude/nova-pinyin`。本地目录可能仍叫 `xpinyin`，不要因此恢复旧项目名或旧仓库地址。
- 当前目标平台是 **Ubuntu 24.04 amd64**，交付形式为可安装的 `.deb`。其他 Ubuntu 版本、CPU 架构及桌面组合只有经过验证才能声明支持。
- 输入引擎使用 **Fcitx5 + LibIME**，核心为 C++20；管理器使用 Python/Tk，用户数据使用 SQLite，繁体转换使用 OpenCC。
- 需求及阶段边界以 `docs/base-requirements.md` 为基础，结合用户已确认的范围。不要未经要求扩展到云输入、AI、同步或编辑器语义集成。
- 开发版本号不代表已经发布。发布状态以 GitHub Release 和对应验证记录为准。

## 开始工作

1. 先查看 `git status --short --branch`、相关差异和当前分支，保留已有未提交修改。
2. 阅读 `README.md`、本次涉及的需求，以及相关说明文档：
   - `docs/backend-decision.md`：后端选择与依赖来源。
   - `docs/advanced-input.md`：上下文、应用提示、开发者补全及项目索引边界。
   - `docs/dictionary-format.md`：词库格式。
   - `docs/install.md`、`docs/validation.md`：安装方法与实际验证状态。
3. 以当前源码、脚本和 `.github/workflows/` 为准核对命令，不依赖历史聊天中的临时路径、测试数量或版本号。
4. 如果命令执行环境失效，明确区分“文件已编辑”“已验证”“已提交”“已推送”“已发布”。保留修改，不把无法执行的检查报告为通过。

## 代码位置与修改原则

| 位置 | 职责 |
| --- | --- |
| `src/core/` | 拼音会话、候选、上下文、开发者补全与持久化 |
| `src/addon/addon.cpp` | Fcitx5 输入事件、输入上下文、候选 UI 与配置 |
| `tools/tool.cpp` | 词库和用户数据命令行接口 |
| `tools/novapinyin-manager.py` | 设置、词库和项目管理界面 |
| `tools/novapinyin-project.py` | 用户显式触发的项目标识符索引 |
| `data/` | Fcitx5 配置、桌面入口与示例词库 |
| `tests/` | 核心、插件事件、索引、图形验证与性能工具 |
| `scripts/build-deb.sh`、`packaging/` | 构建、测试与 Debian 打包 |
| `.github/workflows/` | Ubuntu CI 与预发布流程 |

- 维持现有模块边界和代码风格；C++ 格式遵循仓库格式配置。避免与任务无关的重构和新依赖。
- 输入事件路径不要执行磁盘扫描、联网请求或长时间阻塞操作。项目索引等耗时任务放在后台，界面更新留在主线程。
- 候选标识、会话修订号及后端候选索引必须保持一致。旧候选回调在编辑、重置、失焦或会话切换后不得提交文本。
- 明确 Enter、Tab、空格、数字键及快捷键的消费规则；补全选择不能把用于选择的 Enter 继续传给应用执行命令。
- 失焦、取消及敏感输入场景不得意外提交补全前缀。审查 Fcitx5 的预编辑自动提交行为。
- 注意 LibIME 状态引用的对象寿命。尤其 `UserLanguageModel::score` 的输出状态可能保留词节点指针；不要让状态引用临时对象或被容器扩容移动的对象。涉及此处的修改必须运行内存检查。
- SQLite 迁移保持事务性，覆盖旧版本数据保留、清空和导出。异步索引或学习结果写回时校验数据代次，防止清空后旧任务恢复数据。
- 配置读写应保留未知选项，新增高级功能按现有约定默认关闭，并同步原生配置和管理器。

## 隐私与项目索引

- 输入运行时保持本地处理，不上传按键、候选、周围文本、源码或个人词条；不记录完整输入历史。
- 仅学习实际提交且允许学习的词条。隐私模式和密码／敏感输入应禁止个人词条、学习、上下文及开发者补全。
- 上下文仅在内存中有界保存，按输入上下文隔离，并在失焦、重置及隐私状态变化时清理。不得将其写入日志或数据库。
- 项目索引必须由用户显式选择目录并触发；不要自动扫描家目录、监听源码或执行项目命令、Git hooks、fsmonitor 及补全文本。
- 维持 `.gitignore` 和 `.novapinyinignore` 规则、敏感文件过滤及符号链接防护。文件打开期间也要防止路径被替换后越出所选目录。
- 保持文件大小、累计读取量、目录深度、文件数、词条数和处理时间上限；检查上限时覆盖目录遍历及逐文件处理。
- 持久化项目数据限于已约定的项目名称、根路径、标识符和频次，不保存源码正文或命令历史。
- 尊重 XDG 数据与配置目录。普通卸载保留用户数据；显式清空应覆盖个人学习数据及项目索引，防止后台任务重新写回。
- 测试使用临时数据目录和隔离会话，不操作开发者真实输入法配置、词库、项目索引或用户数据库。

## 构建与验证

在 Ubuntu 24.04 环境中操作。构建依赖见 `README.md` 和 CI 的安装步骤；宿主系统较旧时使用匹配目标版本的隔离环境，避免混用宿主库和目标 ABI。

常规开发构建：

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
```

测试应覆盖实际行为和回归风险。核心或插件变更运行 CTest；项目索引变更同时关注 `tests/project_test.py`；仅修改文档通常无需重新运行完整构建。

内存与未定义行为检查使用独立构建目录，与 CI 保持一致：

```bash
cmake -S . -B build-asan -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
  -DCMAKE_SHARED_LINKER_FLAGS="-fsanitize=address,undefined" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined" \
  -DCMAKE_GTEST_DISCOVER_TESTS_DISCOVERY_MODE=PRE_TEST
cmake --build build-asan --parallel 2
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
  UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
  ctest --test-dir build-asan --output-on-failure
```

不要通过关闭泄漏检测、跳过失败用例或降低检查强度掩盖失败。隔离环境导致的检查限制应明确记录，并使用匹配目标系统的原生 CI 完成验证。

打包入口：

```bash
./scripts/build-deb.sh
```

脚本包含构建、测试、安装暂存和依赖解析，输出到 `dist/`；可用 `NOVA_BUILD_DIR`、`NOVA_PACKAGE_DIR` 指定输出目录。库依赖通过 `dpkg-shlibdeps` 解析，不手写猜测 ABI 包名。版本来源为 `packaging/control.in`，同时保持 `CMakeLists.txt` 的上游版本一致。

涉及按键提交、预编辑或管理器行为时，还需验证安装后的真实程序。安装目标 `.deb` 和图形测试依赖后，在隔离环境中执行：

```bash
cmake -S . -B build -DNOVA_GUI_SMOKE=ON
cmake --build build --parallel 2
NOVA_ISOLATED_GUI_TEST=1 dbus-run-session -- bash tests/gui-smoke.sh
NOVA_ISOLATED_GUI_TEST=1 \
  XDG_DATA_HOME="$(mktemp -d)" XDG_CONFIG_HOME="$(mktemp -d)" \
  xvfb-run -a python3 tests/manager-smoke.py
```

图形测试依赖及安装步骤见 `.github/workflows/release.yml`。测试在独立 D-Bus 和 Xvfb 中运行，避免影响当前桌面；管理器测试读取已安装的 `/usr/bin/novapinyin-manager`，必须确保安装的是本次构建。

## 提交、发布与交付

- 不覆盖已有用户修改，不提交构建目录、数据库、临时索引、私钥或其他凭据。提交前核对差异及暂存范围。
- 不硬编码某个工作区的 SSH 密钥路径或新建／替换凭据。使用现有 remote 和账号配置；只有用户要求时才修改 SSH 配置，并保留已有密钥。
- 推送前运行与变更相关的检查；推送后检查对应提交的 CI 结果。失败时继续修复，并区分本地正常测试与远程 sanitizer 的结果。
- 发布前同步版本、`docs/releases/v<版本>.md`、安装说明和验证记录，验证安装、升级与用户数据保留。更新后重启 Fcitx5 或重新登录以加载新插件。
- 普通构建、内存检查及相关图形验证通过后才能创建发布标签。`build.yml` 和 `release.yml` 均包含 sanitizer 阶段；标签前仍必须确认对应源码提交的构建 CI 已通过内存检查。
- 延续用户已确认的 **Pre-release** 验证方式；除非用户另有要求，不改为正式稳定版。`v*` 标签会触发发布工作流，推送标签属于发布动作。
- 发布资产包含目标 Ubuntu `.deb` 和 `SHA256SUMS`；核对远程资产及校验结果，不把旧包或含未验证修复的包当成最终交付。
- `docs/validation.md` 记录实际环境、检查结果及未验证限制。Xvfb 下 GTK/Qt 通过不能代表所有 GNOME/Wayland、Electron 或编辑器组合已验证。
- 交付说明用中文，简明说明改动、验证、产物位置及尚未解决的问题。不要把“编译通过”当成完整验收，也不要声称未运行的检查已经通过。
