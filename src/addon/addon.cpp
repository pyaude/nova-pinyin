// SPDX-License-Identifier: GPL-3.0-or-later
#include "addon.h"
#include "core/completion.h"
#include "core/core.h"
#include <fcitx-config/configuration.h>
#include <fcitx-config/iniparser.h>
#include <fcitx-config/option.h>
#include <fcitx-utils/event.h>
#include <fcitx-utils/key.h>
#include <fcitx-utils/log.h>
#include <fcitx-utils/utf8.h>
#include <fcitx/addonfactory.h>
#include <fcitx/addonmanager.h>
#include <fcitx/candidatelist.h>
#include <fcitx/event.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputcontextmanager.h>
#include <fcitx/inputcontextproperty.h>
#include <fcitx/inputmethodengine.h>
#include <fcitx/inputpanel.h>
#include <fcitx/instance.h>
#include <fcitx/text.h>
#include <fcitx/userinterface.h>
#include <functional>
#include <future>
#include <map>
namespace nova {
using namespace fcitx;
FCITX_CONFIGURATION(
    Config,
    Option<int, IntConstrain> pageSize{this, "PageSize", "每页候选数", 9, IntConstrain(3, 9)};
    Option<bool> shuangpin{this, "Shuangpin", "启用双拼", false}; Option<int, IntConstrain> profile{
        this, "ShuangpinProfile", "双拼方案：0自然码 / 1小鹤 / 2微软", 0, IntConstrain(0, 2)};
    Option<bool> typo{this, "Typo", "启用有限拼音纠错", true};
    Option<bool> traditional{this, "Traditional", "输出繁体中文", false};
    Option<bool> emoji{this, "Emoji", "显示表情候选", true};
    Option<bool> learning{this, "Learning", "学习用户选词", true};
    Option<bool> privacy{this, "Privacy", "隐私模式（停用个人词条与学习）", false};
    Option<bool> context{this, "Context", "启用内存上下文排序（不保存周围文本）", false};
    Option<bool> applicationHints{this, "ApplicationHints", "启用可覆盖的应用类别提示", false};
    Option<bool> developer{this, "Developer", "启用显式开发者补全", false};
    Option<Key> developerKey{this, "DeveloperKey", "开发者补全快捷键", Key("Control+Alt+space")};
    Option<std::string> activeProject{this, "ActiveProject", "补全使用的项目索引（空为仅内置命令）",
                                      ""};
    Option<std::string> applicationOverrides{this, "ApplicationOverrides",
                                             "应用提示覆盖，如 code=editor;kitty=terminal", ""};
    Option<bool> punctuation{this, "Punctuation", "中文标点（终端默认半角）", true};
    Option<bool> shiftSwitch{this, "ShiftSwitch", "单独按下并释放 Shift 切换中英文", true};
    Option<Key> switchKey{this, "SwitchKey", "额外中英文切换快捷键（默认未设置）", Key()};
    Option<bool> zzh{this, "FuzzyZ_ZH", "模糊音 z / zh", false};
    Option<bool> cch{this, "FuzzyC_CH", "模糊音 c / ch", false};
    Option<bool> ssh{this, "FuzzyS_SH", "模糊音 s / sh", false};
    Option<bool> nl{this, "FuzzyN_L", "模糊音 n / l", false};
    Option<bool> anang{this, "FuzzyAN_ANG", "模糊音 an / ang", false};
    Option<bool> eneng{this, "FuzzyEN_ENG", "模糊音 en / eng", false};
    Option<bool> ining{this, "FuzzyIN_ING", "模糊音 in / ing", false};)
struct InputState : InputContextProperty {
    std::unique_ptr<Session> session;
    std::shared_ptr<Backend> backend;
    ContextBuffer recent;
    CompletionSession completion;
    bool english = false, shiftPending = false, privateMode = false, quote = false,
         singleQuote = false;
};
class Word : public CandidateWord {
  public:
    Word(const Candidate &candidate, std::function<void(InputContext *)> callback)
        : CandidateWord(
              Text(candidate.text +
                   (candidate.annotation.empty() ? "" : " [" + candidate.annotation + "]"))),
          callback_(std::move(callback)) {}
    void select(InputContext *ic) const override { callback_(ic); }

  private:
    std::function<void(InputContext *)> callback_;
};
class CompletionWord : public CandidateWord {
  public:
    CompletionWord(const CompletionCandidate &c, std::function<void(InputContext *)> callback)
        : CandidateWord(Text(c.text + " [" + c.annotation + "]")), callback_(std::move(callback)) {}
    void select(InputContext *ic) const override { callback_(ic); }

  private:
    std::function<void(InputContext *)> callback_;
};
class Engine : public InputMethodEngine {
  public:
    explicit Engine(Instance *instance)
        : instance_(instance), factory_([](InputContext &) { return new InputState; }) {
        backend_ = std::make_shared<Backend>();
        privateBackend_ = std::make_shared<Backend>();
        try {
            store_ = std::make_unique<Store>(dataHome() / "user.db");
        } catch (const std::exception &) {
            error_ = "个人数据不可用，继续基础输入";
        }
        instance_->inputContextManager().registerProperty("novapinyin-state", &factory_);
        for (auto type :
             {EventType::InputContextFocusOut, EventType::InputContextCapabilityChanged})
            watchers_.push_back(instance_->watchEvent(
                type, EventWatcherPhase::PreInputMethod, [this](Event &event) {
                    invalidate(static_cast<InputContextEvent &>(event).inputContext());
                }));
        reloadConfig();
        initialized_ = true;
        timer_ =
            instance_->eventLoop().addTimeEvent(CLOCK_MONOTONIC, now(CLOCK_MONOTONIC) + 1000000, 0,
                                                [this](EventSourceTime *event, uint64_t) {
                                                    refresh();
                                                    event->setNextInterval(1000000);
                                                    event->setEnabled(true);
                                                    return true;
                                                });
    }
    const Configuration *getConfig() const override { return &config_; }
    void setConfig(const RawConfig &raw) override {
        config_.load(raw, true);
        safeSaveAsIni(config_, "conf/novapinyin.conf");
        apply();
    }
    void reloadConfig() override {
        readAsIni(config_, "conf/novapinyin.conf");
        apply();
    }
    std::string subMode(const InputMethodEntry &, InputContext &ic) override {
        auto *s = state(&ic);
        return s->completion.active() ? "补全" : s->english ? "英" : "中";
    }
    void activate(const InputMethodEntry &, InputContextEvent &event) override {
        update(event.inputContext());
    }
    void reset(const InputMethodEntry &, InputContextEvent &event) override {
        auto *ic = event.inputContext();
        auto *s = state(ic);
        s->session->clear();
        s->completion.clear();
        s->recent.clear();
        s->shiftPending = false;
        ic->inputPanel().reset();
        publish(ic);
    }
    void deactivate(const InputMethodEntry &e, InputContextEvent &event) override {
        reset(e, event);
    }
    void keyEvent(const InputMethodEntry &, KeyEvent &event) override {
        auto *ic = event.inputContext();
        auto *s = state(ic);
        auto key = event.key();
        if (ic->capabilityFlags().testAny(
                CapabilityFlags{CapabilityFlag::Password, CapabilityFlag::Disable})) {
            s->session->clear();
            s->completion.clear();
            s->recent.clear();
            ic->inputPanel().reset();
            publish(ic);
            return;
        }
        if (!s->privateMode && *config_.developer && config_.developerKey->sym() != FcitxKey_None &&
            key.check(*config_.developerKey)) {
            if (!event.isRelease() && !key.states().test(KeyState::Repeat)) {
                s->shiftPending = false;
                if (s->completion.active())
                    s->completion.clear();
                else if (s->session->empty()) {
                    std::vector<ProjectTerm> terms;
                    if (applied_)
                        for (const auto &p : applied_->projects)
                            if (p.name == *config_.activeProject)
                                terms = p.terms;
                    s->completion.start(std::move(terms));
                } else {
                    ic->inputPanel().setAuxDown(Text("请先完成当前拼音，再进入补全模式"));
                    publish(ic);
                    event.filterAndAccept();
                    return;
                }
                update(ic);
            }
            event.filterAndAccept();
            return;
        }
        if (s->completion.active()) {
            completionKey(ic, event);
            return;
        }
        if (config_.switchKey->sym() != FcitxKey_None && key.check(*config_.switchKey)) {
            if (!event.isRelease() && !key.states().test(KeyState::Repeat)) {
                if (!s->session->empty()) {
                    commit(ic, s->session->rawCommit());
                    s->session->clear();
                }
                s->english = !s->english;
                s->recent.clear();
                update(ic);
            }
            event.filterAndAccept();
            return;
        }
        if (key.sym() == FcitxKey_Shift_L || key.sym() == FcitxKey_Shift_R) {
            if (!*config_.shiftSwitch)
                return;
            if (event.isRelease()) {
                if (s->shiftPending) {
                    if (!s->session->empty()) {
                        commit(ic, s->session->rawCommit());
                        s->session->clear();
                    }
                    s->english = !s->english;
                    s->recent.clear();
                    s->shiftPending = false;
                    event.filterAndAccept();
                    update(ic);
                }
            } else if (!key.states().testAny(
                           KeyStates{KeyState::Ctrl, KeyState::Alt, KeyState::Super}))
                s->shiftPending = true;
            return;
        }
        s->shiftPending = false;
        if (event.isRelease())
            return;
        if (key.states().testAny(KeyStates{KeyState::Ctrl, KeyState::Alt, KeyState::Super}))
            return;
        if (s->english)
            return;
        auto sym = key.sym();
        if (s->session->empty() &&
            (sym == FcitxKey_Left || sym == FcitxKey_Right || sym == FcitxKey_Home ||
             sym == FcitxKey_End || sym == FcitxKey_BackSpace || sym == FcitxKey_Delete))
            s->recent.clear();
        if ((sym >= FcitxKey_a && sym <= FcitxKey_z) ||
            (!s->session->empty() && sym == FcitxKey_apostrophe)) {
            if (s->session->empty()) {
                if (*config_.context && !s->privateMode) {
                    const auto &surrounding = ic->surroundingText();
                    if (ic->capabilityFlags().test(CapabilityFlag::SurroundingText) &&
                        surrounding.isValid())
                        s->recent.surrounding(surrounding.text(), surrounding.cursor(),
                                              surrounding.anchor());
                    s->session->setContext(s->recent.text());
                } else
                    s->recent.clear();
            }
            if (!s->session->type(Key::keySymToUTF8(sym))) {
                ic->inputPanel().setAuxDown(Text("输入过长，请先选词或按 Enter 提交"));
                publish(ic);
            } else
                update(ic);
            event.filterAndAccept();
            return;
        }
        if (!s->session->empty()) {
            if (sym == FcitxKey_Escape) {
                s->session->clear();
                s->recent.clear();
                event.filterAndAccept();
                update(ic);
                return;
            }
            if (sym == FcitxKey_BackSpace) {
                s->session->backspace();
                event.filterAndAccept();
                update(ic);
                return;
            }
            if (sym == FcitxKey_Delete) {
                s->session->deleteForward();
                event.filterAndAccept();
                update(ic);
                return;
            }
            if (sym == FcitxKey_Left || sym == FcitxKey_Right) {
                s->session->move(sym == FcitxKey_Left ? -1 : 1);
                event.filterAndAccept();
                update(ic);
                return;
            }
            if (sym == FcitxKey_Home || sym == FcitxKey_End) {
                if (sym == FcitxKey_Home)
                    s->session->home();
                else
                    s->session->end();
                event.filterAndAccept();
                update(ic);
                return;
            }
            if (sym == FcitxKey_Return || sym == FcitxKey_KP_Enter) {
                commit(ic, s->session->rawCommit());
                s->session->clear();
                event.filterAndAccept();
                update(ic);
                return;
            }
            auto list = ic->inputPanel().candidateList();
            if (sym == FcitxKey_Page_Up || sym == FcitxKey_Page_Down ||
                sym == FcitxKey_Up || sym == FcitxKey_Down) {
                if (list) {
                    auto *p = list->toPageable();
                    if (p) {
                        if (sym == FcitxKey_Page_Up || sym == FcitxKey_Up)
                            p->prev();
                        else
                            p->next();
                        publish(ic);
                    }
                }
                event.filterAndAccept();
                return;
            }
            if (sym == FcitxKey_space || (sym >= FcitxKey_1 && sym <= FcitxKey_9)) {
                int index = sym == FcitxKey_space ? 0 : static_cast<int>(sym - FcitxKey_1);
                if (list && index < list->size()) {
                    list->candidate(index).select(ic);
                    event.filterAndAccept();
                    return;
                }
                if (!list || list->size() == 0) {
                    commit(ic, s->session->rawCommit());
                    s->session->clear();
                    update(ic);
                    if (sym == FcitxKey_space)
                        event.filterAndAccept();
                    return;
                }
                event.filterAndAccept();
                return;
            }
        }
        auto character = Key::keySymToUTF8(key.sym());
        static const std::map<std::string, std::string> marks = {
            {",", "，"}, {".", "。"}, {"?", "？"}, {"!", "！"}, {":", "："}, {";", "；"},
            {"(", "（"}, {")", "）"}, {"[", "【"}, {"]", "】"}, {"\\", "、"}};
        if (marks.count(character) || character == "\"" || character == "'") {
            if (!s->session->empty()) {
                auto candidates = s->session->candidates();
                if (!candidates.empty()) {
                    auto candidate = candidates.front();
                    choose(ic, candidate.id, candidate.revision);
                }
                if (!s->session->empty()) {
                    commit(ic, s->session->rawCommit());
                    s->session->clear();
                }
            }
            const bool chinese =
                *config_.punctuation && application(ic) != ApplicationClass::Terminal;
            if (chinese) {
                if (character == "\"") {
                    commit(ic, s->quote ? "”" : "“");
                    s->quote = !s->quote;
                } else if (character == "'") {
                    commit(ic, s->singleQuote ? "’" : "‘");
                    s->singleQuote = !s->singleQuote;
                } else
                    commit(ic, marks.at(character));
                event.filterAndAccept();
            }
            update(ic);
            return;
        }
        // Printable non-pinyin input must not leave an old composition behind.
        if (!s->session->empty() && !character.empty()) {
            commit(ic, s->session->rawCommit());
            s->session->clear();
            update(ic);
        }
    }

  private:
    Instance *instance_;
    bool initialized_ = false;
    Config config_;
    FactoryFor<InputState> factory_;
    std::shared_ptr<Backend> backend_, privateBackend_;
    std::unique_ptr<Store> store_;
    std::shared_ptr<const Snapshot> applied_;
    std::future<std::shared_ptr<Backend>> loading_;
    std::shared_ptr<const Snapshot> pending_;
    std::unique_ptr<EventSourceTime> timer_;
    std::string error_;
    std::vector<std::unique_ptr<HandlerTableEntry<EventHandler>>> watchers_;
    ApplicationClass application(InputContext *ic) const {
        const bool terminal = ic->capabilityFlags().test(CapabilityFlag::Terminal);
        if (!*config_.applicationHints || *config_.privacy ||
            ic->capabilityFlags().testAny(CapabilityFlags{
                CapabilityFlag::Password, CapabilityFlag::Sensitive, CapabilityFlag::Disable}))
            return terminal ? ApplicationClass::Terminal : ApplicationClass::General;
        return classifyApplication(ic->program(), terminal, *config_.applicationOverrides);
    }
    void invalidate(InputContext *ic) {
        auto *s = ic->propertyFor(&factory_);
        s->session.reset();
        s->recent.clear();
        s->completion.clear();
        s->shiftPending = false;
        s->quote = s->singleQuote = false;
        if (instance_->inputMethodEngine(ic) == this) {
            ic->inputPanel().reset();
            publish(ic);
        }
    }
    void commit(InputContext *ic, const std::string &text) {
        if (text.empty())
            return;
        ic->commitString(text);
        auto *s = state(ic);
        if (*config_.context && !s->privateMode)
            s->recent.committed(text);
        else
            s->recent.clear();
    }
    void completionKey(InputContext *ic, KeyEvent &event) {
        auto *s = state(ic);
        auto key = event.key();
        s->shiftPending = false;
        if (event.isRelease())
            return;
        // Physical modifier presses precede the trigger chord. Keep the mode until
        // the chord's ordinary key arrives, otherwise the exit chord re-enters it.
        switch (key.sym()) {
        case FcitxKey_Control_L:
        case FcitxKey_Control_R:
        case FcitxKey_Alt_L:
        case FcitxKey_Alt_R:
        case FcitxKey_Super_L:
        case FcitxKey_Super_R:
        case FcitxKey_Meta_L:
        case FcitxKey_Meta_R:
        case FcitxKey_Shift_L:
        case FcitxKey_Shift_R:
        case FcitxKey_Caps_Lock:
            return;
        default:
            break;
        }
        if (key.states().testAny(KeyStates{KeyState::Ctrl, KeyState::Alt, KeyState::Super})) {
            s->completion.clear();
            s->recent.clear();
            update(ic);
            return;
        }
        auto sym = key.sym();
        if (sym == FcitxKey_Escape) {
            s->completion.clear();
            s->recent.clear();
        } else if (sym == FcitxKey_Return || sym == FcitxKey_KP_Enter || sym == FcitxKey_Tab) {
            auto list = ic->inputPanel().candidateList();
            if (list && list->size() > 0) {
                auto index = list->cursorIndex();
                list->candidate(index < 0 ? 0 : index).select(ic);
            } else {
                auto raw = s->completion.raw();
                s->completion.clear();
                commit(ic, raw);
            }
        } else if (sym == FcitxKey_Up || sym == FcitxKey_Down || sym == FcitxKey_Page_Up ||
                   sym == FcitxKey_Page_Down) {
            auto list = ic->inputPanel().candidateList();
            if (list) {
                if (sym == FcitxKey_Up || sym == FcitxKey_Down) {
                    auto *cursor = list->toCursorMovable();
                    if (cursor) {
                        if (sym == FcitxKey_Up)
                            cursor->prevCandidate();
                        else
                            cursor->nextCandidate();
                    }
                } else if (auto *pages = list->toPageable()) {
                    if (sym == FcitxKey_Page_Up)
                        pages->prev();
                    else
                        pages->next();
                }
                publish(ic);
            }
            event.filterAndAccept();
            return;
        } else if (sym == FcitxKey_BackSpace)
            s->completion.backspace();
        else if (sym == FcitxKey_Delete)
            s->completion.deleteForward();
        else if (sym == FcitxKey_Left || sym == FcitxKey_Right)
            s->completion.move(sym == FcitxKey_Left ? -1 : 1);
        else if (sym == FcitxKey_Home)
            s->completion.home();
        else if (sym == FcitxKey_End)
            s->completion.end();
        else {
            auto text = Key::keySymToUTF8(sym);
            if (text.empty()) {
                s->completion.clear();
                s->recent.clear();
                update(ic);
                return;
            }
            if (!s->completion.type(text)) {
                ic->inputPanel().setAuxDown(Text("补全输入限 128 个 ASCII 字符"));
                publish(ic);
                event.filterAndAccept();
                return;
            }
        }
        update(ic);
        event.filterAndAccept();
    }
    void chooseCompletion(InputContext *ic, uint64_t id, uint64_t revision) {
        if (!ic->hasFocus() || !*config_.developer)
            return;
        auto *s = state(ic);
        if (s->privateMode)
            return;
        if (auto result = s->completion.select(id, revision))
            commit(ic, *result);
        update(ic);
    }
    Options options() const {
        Options o;
        o.shuangpin = *config_.shuangpin;
        o.profile = *config_.profile;
        o.typo = *config_.typo;
        o.traditional = *config_.traditional;
        o.emoji = *config_.emoji;
        o.zzh = *config_.zzh;
        o.cch = *config_.cch;
        o.ssh = *config_.ssh;
        o.nl = *config_.nl;
        o.anang = *config_.anang;
        o.eneng = *config_.eneng;
        o.ining = *config_.ining;
        return o;
    }
    void apply() {
        backend_->configure(options());
        privateBackend_->configure(options());
        // Existing contexts have no NovaPinyin sessions during construction.
        // Querying their engine here would recursively load this on-demand addon.
        if (!initialized_)
            return;
        instance_->inputContextManager().foreach ([this](InputContext *ic) {
            invalidate(ic);
            if (ic->hasFocus() && instance_->inputMethodEngine(ic) == this) {
                ic->inputPanel().reset();
                publish(ic);
            }
            return true;
        });
    }
    InputState *state(InputContext *ic) {
        auto *s = ic->propertyFor(&factory_);
        bool privacy = *config_.privacy || ic->capabilityFlags().testAny(CapabilityFlags{
                                               CapabilityFlag::Password, CapabilityFlag::Sensitive,
                                               CapabilityFlag::Disable});
        auto backend = privacy ? privateBackend_ : backend_;
        if (!s->session || s->privateMode != privacy) {
            s->recent.clear();
            s->completion.clear();
            s->backend = backend;
            s->privateMode = privacy;
            s->session = std::make_unique<Session>(backend);
            auto o = options();
            if (privacy)
                o.emoji = false;
            s->session->configure(o);
        }
        return s;
    }
    void publish(InputContext *ic) {
        ic->updatePreedit();
        ic->updateUserInterface(UserInterfaceComponent::InputPanel);
    }
    void choose(InputContext *ic, uint64_t id, uint64_t revision) {
        if (!ic->hasFocus() || ic->capabilityFlags().testAny(CapabilityFlags{
                                   CapabilityFlag::Password, CapabilityFlag::Disable}))
            return;
        auto *s = state(ic);
        auto result = s->session->select(id, revision);
        if (result) {
            commit(ic, *result);
            if (store_ && *config_.learning && !s->privateMode) {
                for (const auto &p : s->session->learning()) {
                    try {
                        store_->learn(p);
                    } catch (const std::exception &) {
                        error_ = "学习写入失败，继续基础输入";
                    }
                }
            }
        }
        update(ic);
    }
    void update(InputContext *ic) {
        auto *s = state(ic);
        ic->inputPanel().reset();
        if (s->completion.active()) {
            Text preedit(s->completion.raw(), TextFormatFlag::DontCommit);
            preedit.setCursor(s->completion.cursor());
            ic->inputPanel().setClientPreedit(preedit);
            ic->inputPanel().setPreedit(preedit);
            auto list = std::make_unique<CommonCandidateList>();
            list->setPageSize(*config_.pageSize);
            list->setLayoutHint(CandidateLayoutHint::Vertical);
            list->setCursorIncludeUnselected(false);
            for (const auto &c : s->completion.candidates())
                list->append(std::make_unique<CompletionWord>(
                    c, [this, id = c.id, revision = c.revision](InputContext *target) {
                        chooseCompletion(target, id, revision);
                    }));
            if (list->totalSize())
                list->setGlobalCursorIndex(0);
            ic->inputPanel().setCandidateList(std::move(list));
            std::string mode = "补全 · Space 输入空格 · ↑↓ 选择 · Tab/Enter 提交文字 · Esc 退出";
            if (!config_.activeProject->empty())
                mode += " · 项目 " + *config_.activeProject;
            ic->inputPanel().setAuxDown(Text(mode));
            publish(ic);
            return;
        }
        if (!s->session->empty()) {
            auto [text, cursor] = s->session->preedit();
            Text preedit(text);
            preedit.setCursor(cursor);
            ic->inputPanel().setClientPreedit(preedit);
            ic->inputPanel().setPreedit(preedit);
            auto list = std::make_unique<CommonCandidateList>();
            list->setPageSize(*config_.pageSize);
            list->setLabels({"1", "2", "3", "4", "5", "6", "7", "8", "9"});
            list->setLayoutHint(CandidateLayoutHint::Horizontal);
            for (const auto &c : s->session->candidates())
                list->append(std::make_unique<Word>(
                    c, [this, id = c.id, revision = c.revision](InputContext *target) {
                        choose(target, id, revision);
                    }));
            if (list->totalSize())
                list->setGlobalCursorIndex(0);
            ic->inputPanel().setCandidateList(std::move(list));
        }
        std::string mode = s->english ? "英文" : (*config_.shuangpin ? "双拼" : "全拼");
        if (s->privateMode)
            mode += " · 隐私";
        if (*config_.traditional)
            mode += " · 繁体";
        if (*config_.context && !s->privateMode && !s->recent.text().empty())
            mode += " · 内存上下文";
        if (application(ic) == ApplicationClass::Terminal)
            mode += " · 终端提示";
        else if (application(ic) == ApplicationClass::Editor)
            mode += " · 编辑器提示";
        if (!error_.empty())
            mode += " · " + error_;
        if (!s->session->empty())
            ic->inputPanel().setAuxDown(Text(mode));
        publish(ic);
    }
    void refresh() {
        if (!store_)
            return;
        auto snapshot = store_->snapshot();
        error_ = snapshot->error;
        if (loading_.valid() &&
            loading_.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
            try {
                auto next = loading_.get();
                next->configure(options());
                backend_ = std::move(next);
                applied_ = pending_;
                error_.clear();
                // Retain an in-progress composition/completion and its candidate mapping.
                // New sessions use the new dictionary. Explicit data clearing invalidates old
                // sessions.
                bool epochChanged =
                    applied_ && lastGeneration_ && applied_->generation != lastGeneration_;
                lastGeneration_ = applied_->generation;
                instance_->inputContextManager().foreach ([this, epochChanged](InputContext *ic) {
                    auto *s = ic->propertyFor(&factory_);
                    if (epochChanged || !s->session ||
                        (s->session->empty() && !s->completion.active())) {
                        invalidate(ic);
                        if (ic->hasFocus())
                            update(ic);
                    }
                    return true;
                });
            } catch (const std::exception &) {
                error_ = "词库加载失败，保留当前词库";
            }
        }
        if (!loading_.valid() && snapshot->error.empty() && snapshot != applied_ &&
            snapshot->generation) {
            pending_ = snapshot;
            loading_ = std::async(std::launch::async, [snapshot] {
                return std::make_shared<Backend>(snapshot->phrases);
            });
        }
    }
    uint64_t lastGeneration_ = 0;
};
std::unique_ptr<InputMethodEngine> createEngine(Instance *i) { return std::make_unique<Engine>(i); }
class Factory : public AddonFactory {
  public:
    AddonInstance *create(AddonManager *m) override { return new Engine(m->instance()); }
};
} // namespace nova
FCITX_ADDON_FACTORY(nova::Factory)
