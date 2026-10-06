// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/completion.h"
#include "core/context.h"
#include "core/core.h"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <set>
#include <sqlite3.h>
using namespace nova;
namespace {
std::filesystem::path path() {
    auto pattern = (std::filesystem::temp_directory_path() / "nova-advanced-XXXXXX").string();
    auto *created = mkdtemp(pattern.data());
    if (!created)
        throw std::runtime_error("Cannot create isolated test directory");
    return created;
}
} // namespace
TEST(Completion, ExplicitModeAndSpaces) {
    CompletionSession s;
    EXPECT_FALSE(s.type("git che"));
    s.start();
    ASSERT_TRUE(s.type("git che"));
    const auto candidates = s.candidates();
    ASSERT_EQ(candidates.size(), 2);
    auto c = std::find_if(candidates.begin(), candidates.end(),
                          [](const auto &c) { return c.text == "git checkout"; });
    ASSERT_NE(c, candidates.end());
    EXPECT_EQ(s.select(c->id, c->revision), "git checkout");
    EXPECT_FALSE(s.active());
    EXPECT_FALSE(s.select(c->id, c->revision));
}
TEST(Completion, ProjectRankingAndStaleSelection) {
    CompletionSession s;
    s.start({{"process_input", 10}, {"process_context", 2}, {"OtherFunction", 30}});
    s.type("process_");
    auto candidates = s.candidates();
    ASSERT_EQ(candidates.size(), 2);
    EXPECT_EQ(candidates.front().text, "process_input");
    auto old = candidates.front();
    s.type("c");
    EXPECT_FALSE(s.select(old.id, old.revision));
    auto updated = s.candidates();
    ASSERT_EQ(updated.size(), 1);
    EXPECT_EQ(s.select(updated[0].id, updated[0].revision), "process_context");
    s.start({{"process_new", 1}});
    s.type("process_");
    s.candidates();
    EXPECT_FALSE(s.select(old.id, old.revision));
}
TEST(Completion, CmakeEditingAndBoundaries) {
    CompletionSession s;
    s.start();
    s.type("cmake ta");
    auto values = s.candidates();
    EXPECT_TRUE(std::any_of(values.begin(), values.end(),
                            [](const auto &c) { return c.text == "target_link_libraries"; }));
    s.home();
    s.deleteForward();
    s.type("c");
    s.end();
    EXPECT_EQ(s.raw(), "cmake ta");
    s.backspace();
    EXPECT_EQ(s.raw(), "cmake t");
    s.clear();
    s.start();
    EXPECT_TRUE(s.type(std::string(128, 'x')));
    EXPECT_FALSE(s.type("x"));
    EXPECT_FALSE(s.type("\n"));
    EXPECT_FALSE(s.type("中文"));
}
TEST(Context, UnicodeBoundsAndSelection) {
    ContextBuffer context;
    for (int i = 0; i < 300; ++i)
        context.committed("你");
    EXPECT_EQ(context.text().size(), ContextBuffer::limit * 3);
    EXPECT_TRUE(context.surrounding("你好世界", 2, 2));
    EXPECT_EQ(context.text(), "你好");
    EXPECT_FALSE(context.surrounding("你好", 1, 2));
    EXPECT_TRUE(context.text().empty());
    EXPECT_FALSE(context.surrounding("你好", 3, 3));
    context.committed(std::string("\xff"));
    EXPECT_TRUE(context.text().empty());
    context.committed(std::string(65537, 'a'));
    EXPECT_TRUE(context.text().empty());
    EXPECT_TRUE(context.surrounding("", 0, 0));
}
TEST(Context, SessionFreezeResetAndCandidateMapping) {
    auto backend = std::make_shared<Backend>();
    Session s(backend), plain(backend);
    s.setContext("银行");
    s.type("zhang");
    plain.type("zhang");
    std::set<std::pair<std::string, size_t>> hinted, baseline;
    const auto candidates = s.candidates();
    for (const auto &c : candidates)
        hinted.emplace(c.text, c.end);
    for (const auto &c : plain.candidates())
        baseline.emplace(c.text, c.end);
    EXPECT_EQ(hinted, baseline);
    auto revision = s.revision();
    s.setContext("完全不同");
    EXPECT_EQ(s.contextHint(), "银行");
    EXPECT_EQ(s.revision(), revision);
    ASSERT_FALSE(candidates.empty());
    EXPECT_EQ(s.select(candidates.front().id, candidates.front().revision),
              candidates.front().text);
    EXPECT_TRUE(s.contextHint().empty());
}
TEST(Context, ApplicationHintsAndOverrides) {
    EXPECT_EQ(classifyApplication("", false, ""), ApplicationClass::General);
    EXPECT_EQ(classifyApplication("", true, ""), ApplicationClass::Terminal);
    EXPECT_EQ(classifyApplication("/usr/bin/Code", false, ""), ApplicationClass::Editor);
    EXPECT_EQ(classifyApplication("kitty", true, "kitty=general"), ApplicationClass::General);
    EXPECT_EQ(classifyApplication("firefox", false, "code=editor;firefox=terminal"),
              ApplicationClass::Terminal);
    EXPECT_EQ(classifyApplication("firefox", false, "firefox=unknown"), ApplicationClass::General);
}
TEST(Context, RankingRespondsToLanguageModelHint) {
    auto backend = std::make_shared<Backend>();
    bool changed = false;
    for (const auto *reading : {"shi", "zhang", "hang", "li", "gong"}) {
        Session plain(backend);
        plain.type(reading);
        auto original = plain.candidates();
        for (const auto *hint : {"银行", "一", "公司", "这里", "今天", "我"}) {
            Session hinted(backend);
            hinted.setContext(hint);
            hinted.type(reading);
            auto candidates = hinted.candidates();
            ASSERT_EQ(candidates.size(), original.size());
            for (size_t i = 0; i < candidates.size(); ++i)
                changed |= candidates[i].text != original[i].text;
        }
    }
    EXPECT_TRUE(changed);
}
TEST(Context, LongHintRepeatedEditsAndBackendRefresh) {
    // Exercise successive LM states after each temporary hint word has died.
    // Older sessions must remain usable while the addon replaces its backend.
    auto backend = std::make_shared<Backend>();
    Session original(backend);
    original.setContext("银行公司今天这里银行公司今天这里银行公司今天这里");
    original.type("zhang");
    const auto old = original.candidates();
    ASSERT_FALSE(old.empty());
    backend = std::make_shared<Backend>(std::vector<Phrase>{{"ni hao", "你好", 20, 0}});
    Session refreshed(backend);
    for (int round = 0; round < 5; ++round) {
        refreshed.setContext("今天我在银行公司办理业务这里需要你好世界");
        for (char key : std::string("nihao")) {
            ASSERT_TRUE(refreshed.type(std::string(1, key)));
            ASSERT_FALSE(refreshed.candidates().empty());
        }
        refreshed.backspace();
        refreshed.candidates();
        refreshed.type("o");
        const auto values = refreshed.candidates();
        ASSERT_FALSE(values.empty());
        EXPECT_EQ(refreshed.select(values.front().id, values.front().revision),
                  values.front().text);
        EXPECT_TRUE(refreshed.contextHint().empty());
    }
    EXPECT_EQ(original.select(old.front().id, old.front().revision), old.front().text);
}
TEST(Project, PersistenceMigrationExportAndClear) {
    auto folder = path();
    const auto dbpath = folder / "user.db";
    {
        Database db(dbpath);
        db.learn({"ni hao", "你好", 1, 0});
        db.importDictionary("domain", {{"zhong guo", "中国", 0, 5}});
    }
    sqlite3 *raw = nullptr;
    ASSERT_EQ(sqlite3_open(dbpath.c_str(), &raw), SQLITE_OK);
    ASSERT_EQ(sqlite3_exec(raw,
                           "DROP TABLE project_term; DROP TABLE project; PRAGMA user_version=1",
                           nullptr, nullptr, nullptr),
              SQLITE_OK);
    sqlite3_close(raw);
    {
        Database db(dbpath);
        EXPECT_EQ(db.phrases().size(), 2);
        db.importProject("demo", folder.string(), {{"ProcessInput", 2}, {"ProjectClass", 10}});
        auto projects = db.projects();
        ASSERT_EQ(projects.size(), 1);
        EXPECT_EQ(projects[0].terms.size(), 2);
        db.exportUser(folder / "export.tsv");
        EXPECT_EQ(readDictionary(folder / "export.tsv").size(), 1);
    }
    Database db(dbpath);
    ASSERT_EQ(db.projects().size(), 1);
    auto generation = db.generation();
    db.clear();
    EXPECT_TRUE(db.projects().empty());
    EXPECT_EQ(db.phrases().size(), 1); // installed domain dictionary survives
    EXPECT_THROW(db.importProject("old", folder.string(), {{"StaleSymbol", 1}}, generation),
                 std::runtime_error);
    EXPECT_TRUE(db.projects().empty());
}
TEST(Project, TransactionFailureAndRemoval) {
    auto folder = path();
    Database db(folder / "user.db");
    db.importProject("demo", folder.string(), {{"OriginalSymbol", 5}});
    EXPECT_THROW(db.importProject("demo", folder.string(), {{"invalid symbol", 1}}),
                 std::runtime_error);
    ASSERT_EQ(db.projects().size(), 1);
    EXPECT_EQ(db.projects()[0].terms[0].text, "OriginalSymbol");
    EXPECT_THROW(db.importProject("demo", "relative", {{"ValidSymbol", 1}}), std::runtime_error);
    db.removeProject("demo");
    EXPECT_TRUE(db.projects().empty());
}
