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
    auto engine = nova::createEngine(&instance);
    auto startup = instance.eventLoop().addTimeEvent(
        CLOCK_MONOTONIC, now(CLOCK_MONOTONIC) + 2000000, 0, [&](EventSourceTime *, uint64_t) {
            instance.eventLoop().exit();
            return false;
        });
    instance.eventLoop().exec();
    Client client(instance.inputContextManager());
    InputMethodEntry entry("novapinyin", "NovaPinyin", "zh_CN", "novapinyin");
    auto send = [&](const std::string &key, bool release = false) {
        KeyEvent event(&client, Key(key), release);
        engine->keyEvent(entry, event);
        return event.accepted();
    };
    for (char c : std::string("nihao"))
        send(std::string(1, c));
    ASSERT_TRUE(client.inputPanel().candidateList());
    EXPECT_GT(client.inputPanel().candidateList()->size(), 0);
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
    engine.reset();
    nova::Database db(dir / "novapinyin/user.db");
    const auto phrases = db.phrases();
    ASSERT_EQ(phrases.size(), 1);
    EXPECT_EQ(phrases.front().text, "你好");
    EXPECT_EQ(phrases.front().count, 1);
}
