// SPDX-License-Identifier: GPL-3.0-or-later
#include "addon/addon.h"
#include "core/store.h"
#include <fcitx-utils/event.h>
#include <fcitx/candidatelist.h>
#include <fcitx/event.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputcontextmanager.h>
#include <fcitx/inputmethodengine.h>
#include <fcitx/inputpanel.h>
#include <fcitx/instance.h>
#include <filesystem>
#include <gtest/gtest.h>
#include <thread>
#include <unistd.h>
using namespace fcitx;
class Client : public InputContext {
  public:
    Client(InputContextManager &m) : InputContext(m, "nova-test") {
        setCapabilityFlags(CapabilityFlag::Preedit);
        focusIn();
    }
    ~Client() { destroy(); }
    const char *frontend() const override { return "test"; }
    void commitStringImpl(const std::string &s) override { committed += s; }
    void deleteSurroundingTextImpl(int, unsigned int) override {}
    void forwardKeyImpl(const ForwardKeyEvent &) override {}
    void updatePreeditImpl() override {}
    std::string committed;
};
TEST(Addon, EventsAndPrivacy) {
    auto dir = std::filesystem::temp_directory_path() / ("nova-addon-" + std::to_string(getpid()));
    std::filesystem::create_directories(dir);
    setenv("XDG_DATA_HOME", dir.c_str(), 1);
    setenv("XDG_CONFIG_HOME", dir.c_str(), 1);
    char program[] = "nova-test";
    char disabled[] = "--disable=all";
    char *argv[] = {program, disabled, nullptr};
    Instance instance(2, argv);
    instance.initialize();
    {
        nova::Database database(dir / "novapinyin/user.db");
        database.importProject("addon-project", dir.string(),
                               {{"ProjectSymbolFoo", 10}, {"ProjectSymbolBar", 2}});
    }
    auto engine = nova::createEngine(&instance);
    Client client(instance.inputContextManager());
    InputMethodEntry entry("novapinyin", "NovaPinyin", "zh_CN", "novapinyin");
    auto send = [&](const std::string &key, bool release = false) {
        KeyEvent event(&client, Key(key), release);
        engine->keyEvent(entry, event);
        return event.accepted();
    };
    EXPECT_FALSE(send("Control+Alt+space")); // verify the untouched default before probing
    RawConfig startupConfig;
    startupConfig.setValueByPath("Developer", "True");
    startupConfig.setValueByPath("ActiveProject", "addon-project");
    engine->setConfig(startupConfig);
    // Drive real asynchronous loading until the expected project is visible.
    // Learning assertions also require the initial store snapshot to be ready.
    bool projectReady = false;
    const auto deadline = now(CLOCK_MONOTONIC) + 20000000;
    auto projectWait = instance.eventLoop().addTimeEvent(
        CLOCK_MONOTONIC, now(CLOCK_MONOTONIC) + 100000, 0,
        [&](EventSourceTime *event, uint64_t) {
            send("Escape");
            send("Control+Alt+space");
            for (char c : std::string("ProjectSym"))
                send(std::string(1, c));
            const auto list = client.inputPanel().candidateList();
            projectReady = list && list->size() > 0;
            if (projectReady || now(CLOCK_MONOTONIC) >= deadline) {
                instance.eventLoop().exit();
                return false;
            }
            event->setNextInterval(100000);
            event->setEnabled(true);
            return true;
        });
    instance.eventLoop().exec();
    ASSERT_TRUE(projectReady) << "Project candidates were not loaded within 20 seconds";
    send("Escape");
    startupConfig.setValueByPath("Developer", "False");
    startupConfig.setValueByPath("ActiveProject", "");
    engine->setConfig(startupConfig);
    for (char c : std::string("nihao"))
        send(std::string(1, c));
    ASSERT_TRUE(client.inputPanel().candidateList());
    EXPECT_GT(client.inputPanel().candidateList()->size(), 0);
    EXPECT_EQ(client.inputPanel().candidateList()->layoutHint(), CandidateLayoutHint::Horizontal);
    EXPECT_EQ(client.inputPanel().candidateList()->candidate(0).text().toString(), "你好");
    EXPECT_TRUE(send("space"));
    EXPECT_EQ(client.committed, "你好");
    for (char c : std::string("abc"))
        send(std::string(1, c));
    send("Escape");
    EXPECT_EQ(client.committed, "你好");
    for (char c : std::string("nihao"))
        send(std::string(1, c));
    send("Return");
    EXPECT_EQ(client.committed, "你好nihao");
    EXPECT_FALSE(send("Return"));
    send("Shift_L");
    send("Shift_L", true);
    EXPECT_FALSE(send("a"));
    send("Shift_L");
    send("Shift_L", true);
    EXPECT_FALSE(send("Control+c"));
    client.setCapabilityFlags(CapabilityFlag::Password);
    EXPECT_FALSE(send("n"));
    EXPECT_FALSE(client.inputPanel().candidateList());
    client.setCapabilityFlags(CapabilityFlags{CapabilityFlag::Preedit, CapabilityFlag::Sensitive});
    for (char c : std::string("nihao"))
        send(std::string(1, c));
    send("space");
    EXPECT_EQ(client.committed, "你好nihao你好");
    client.setCapabilityFlags(CapabilityFlag::Preedit);
    RawConfig noLearning;
    noLearning.setValueByPath("Learning", "False");
    engine->setConfig(noLearning);
    for (char c : std::string("zhongguo"))
        send(std::string(1, c));
    send("space");
    EXPECT_EQ(client.committed, "你好nihao你好中国");
    EXPECT_FALSE(send("Control+Alt+space")); // developer capability defaults to disabled
    RawConfig advanced;
    advanced.setValueByPath("Developer", "True");
    advanced.setValueByPath("Context", "True");
    advanced.setValueByPath("Learning", "False");
    engine->setConfig(advanced);
    EXPECT_TRUE(send("Control+Alt+space"));
    for (char c : std::string("git che"))
        send(c == ' ' ? "space" : std::string(1, c));
    EXPECT_EQ(client.committed, "你好nihao你好中国");
    ASSERT_TRUE(client.inputPanel().candidateList());
    EXPECT_TRUE(send("Return"));
    EXPECT_EQ(client.committed, "你好nihao你好中国git checkout");
    EXPECT_FALSE(send("Return")); // only an additional Enter reaches the application
    send("Control+Alt+space");
    send("Control_L");
    send("Alt_L");
    EXPECT_EQ(engine->subMode(entry, client), "补全");
    send("Control+Alt+space");
    EXPECT_EQ(engine->subMode(entry, client), "中");
    EXPECT_TRUE(send("Control+Alt+space"));
    for (char c : std::string("git che"))
        send(c == ' ' ? "space" : std::string(1, c));
    auto oldList = client.inputPanel().candidateList();
    ASSERT_TRUE(oldList);
    send("Escape");
    oldList->candidate(0).select(&client);
    EXPECT_EQ(client.committed, "你好nihao你好中国git checkout");
    send("Control+Alt+space");
    for (char c : std::string("release123"))
        send(std::string(1, c));
    send("Return");
    EXPECT_EQ(client.committed, "你好nihao你好中国git checkoutrelease123");
    send("Control+Alt+space");
    for (char c : std::string("git che"))
        send(c == ' ' ? "space" : std::string(1, c));
    oldList = client.inputPanel().candidateList();
    client.focusOut();
    client.focusIn();
    oldList->candidate(0).select(&client);
    EXPECT_EQ(client.committed, "你好nihao你好中国git checkoutrelease123");
    client.setCapabilityFlags(CapabilityFlags{CapabilityFlag::Preedit, CapabilityFlag::Sensitive});
    EXPECT_FALSE(send("Control+Alt+space"));
    client.setCapabilityFlags(CapabilityFlag::Preedit);
    RawConfig privacy;
    privacy.setValueByPath("Privacy", "True");
    engine->setConfig(privacy);
    EXPECT_FALSE(send("Control+Alt+space"));
    privacy.setValueByPath("Privacy", "False");
    engine->setConfig(privacy);
    send("Control+Alt+space");
    EXPECT_FALSE(send("Control+c")); // clipboard shortcut passes through and cancels completion
    EXPECT_FALSE(send("Return"));
    // Optional surrounding text is not learned or exported.
    client.setCapabilityFlags(
        CapabilityFlags{CapabilityFlag::Preedit, CapabilityFlag::SurroundingText});
    client.surroundingText().setText("银行", 2, 2);
    client.updateSurroundingText();
    for (char c : std::string("zhang"))
        send(std::string(1, c));
    auto frozenList = client.inputPanel().candidateList();
    ASSERT_TRUE(frozenList);
    client.surroundingText().setText("不同文字", 4, 4);
    client.updateSurroundingText();
    EXPECT_EQ(client.inputPanel().candidateList(), frozenList);
    send("Escape");
    RawConfig projectConfig;
    projectConfig.setValueByPath("ActiveProject", "addon-project");
    engine->setConfig(projectConfig);
    send("Control+Alt+space");
    for (char c : std::string("ProjectSym"))
        send(std::string(1, c));
    ASSERT_TRUE(client.inputPanel().candidateList());
    ASSERT_GT(client.inputPanel().candidateList()->size(), 0);
    send("Tab");
    EXPECT_TRUE(client.committed.ends_with("ProjectSymbolFoo"));
    RawConfig apps;
    apps.setValueByPath("ApplicationHints", "True");
    apps.setValueByPath("ApplicationOverrides", "nova-test=terminal");
    engine->setConfig(apps);
    EXPECT_FALSE(send("comma"));
    apps.setValueByPath("ApplicationOverrides", "nova-test=general");
    engine->setConfig(apps);
    EXPECT_TRUE(send("comma"));
    EXPECT_TRUE(client.committed.ends_with("，"));
    engine.reset();
    nova::Database db(dir / "novapinyin/user.db");
    const auto phrases = db.phrases();
    ASSERT_EQ(phrases.size(), 1);
    EXPECT_EQ(phrases.front().text, "你好");
    EXPECT_EQ(phrases.front().count, 1);
}
TEST(Addon, LearningRefreshPreservesCompletion) {
    auto dir = std::filesystem::temp_directory_path() /
               ("nova-addon-refresh-" + std::to_string(getpid()));
    std::filesystem::create_directories(dir);
    setenv("XDG_DATA_HOME", dir.c_str(), 1);
    setenv("XDG_CONFIG_HOME", dir.c_str(), 1);
    char program[] = "nova-refresh-test";
    char disabled[] = "--disable=all";
    char *argv[] = {program, disabled, nullptr};
    Instance instance(2, argv);
    instance.initialize();
    {
        nova::Database database(dir / "novapinyin/user.db");
        database.importProject("refresh-project", dir.string(), {{"ReadySymbol", 1}});
    }
    auto engine = nova::createEngine(&instance);
    Client client(instance.inputContextManager());
    InputMethodEntry entry("novapinyin", "NovaPinyin", "zh_CN", "novapinyin");
    RawConfig config;
    config.setValueByPath("Developer", "True");
    config.setValueByPath("Learning", "True");
    config.setValueByPath("Privacy", "False");
    config.setValueByPath("ActiveProject", "refresh-project");
    engine->setConfig(config);
    auto send = [&](const std::string &key) {
        KeyEvent event(&client, Key(key), false);
        engine->keyEvent(entry, event);
        return event.accepted();
    };
    auto type = [&](const std::string &text) {
        for (char key : text)
            send(key == ' ' ? "space" : std::string(1, key));
    };
    bool started = false, retained = true;
    uint64_t refreshStarted = 0;
    std::shared_ptr<CandidateList> visible;
    const auto deadline = now(CLOCK_MONOTONIC) + 20000000;
    // Keep one event loop running through both startup and the learning refresh.
    auto probe = instance.eventLoop().addTimeEvent(
        CLOCK_MONOTONIC, now(CLOCK_MONOTONIC) + 100000, 0,
        [&](EventSourceTime *event, uint64_t) {
            if (!started) {
                send("Escape");
                send("Control+Alt+space");
                type("ReadySym");
                const auto list = client.inputPanel().candidateList();
                if (list && list->size() > 0) {
                    send("Escape");
                    type("nihao");
                    send("space"); // ordinary learning changes the snapshot, not its generation
                    send("Control+Alt+space");
                    type("git che");
                    visible = client.inputPanel().candidateList();
                    started = true;
                    refreshStarted = now(CLOCK_MONOTONIC);
                }
            } else {
                retained = engine->subMode(entry, client) == "补全" &&
                           client.inputPanel().candidateList() == visible;
                if (!retained || now(CLOCK_MONOTONIC) - refreshStarted >= 8000000) {
                    instance.eventLoop().exit();
                    return false;
                }
            }
            if (now(CLOCK_MONOTONIC) >= deadline) {
                instance.eventLoop().exit();
                return false;
            }
            event->setNextInterval(100000);
            event->setEnabled(true);
            return true;
        });
    instance.eventLoop().exec();
    ASSERT_TRUE(started) << "Initial project snapshot was not loaded within 20 seconds";
    ASSERT_TRUE(retained) << "Ordinary learning cancelled or replaced visible completion";
    ASSERT_TRUE(visible);
    ASSERT_GT(visible->size(), 0);
    EXPECT_EQ(client.committed, "你好");
    EXPECT_TRUE(send("Return"));
    EXPECT_EQ(client.committed, "你好git checkout");
    EXPECT_FALSE(send("Return"));
}
