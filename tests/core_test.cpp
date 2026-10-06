// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/core.h"
#include <fcitx-utils/utf8.h>
#include <cstdlib>
#include <fstream>
#include <future>
#include <gtest/gtest.h>
using namespace nova;
namespace {
std::filesystem::path directory() {
    auto pattern = (std::filesystem::temp_directory_path() / "nova-test-XXXXXX").string();
    auto *created = mkdtemp(pattern.data());
    if (!created)
        throw std::runtime_error("Cannot create isolated test directory");
    return created;
}
std::shared_ptr<Backend> base() {
    static auto b = std::make_shared<Backend>();
    b->configure(Options{});
    return b;
}
Candidate find(Session &s, const std::string &text) {
    for (const auto &c : s.candidates())
        if (c.text == text)
            return c;
    throw std::runtime_error("missing candidate: " + text);
}
} // namespace
TEST(Pinyin, WholeSentenceAndSelection) {
    Session s(base());
    s.configure({});
    ASSERT_TRUE(s.type("nihao"));
    auto c = find(s, "你好");
    auto result = s.select(c.id, c.revision);
    ASSERT_TRUE(result);
    EXPECT_EQ(*result, "你好");
    ASSERT_EQ(s.learning().size(), 1);
    EXPECT_EQ(s.learning()[0].reading, "ni hao");
    EXPECT_TRUE(s.empty());
}
TEST(Pinyin, SentenceComposition) {
    Session s(base());
    s.type("woaizhongguo");
    auto c = find(s, "我爱中国");
    auto r = s.select(c.id, c.revision);
    ASSERT_TRUE(r);
    EXPECT_EQ(*r, "我爱中国");
}
TEST(Pinyin, PartialSelectionAndUndo) {
    Session s(base());
    s.type("nihao");
    auto c = find(s, "你");
    EXPECT_FALSE(s.select(c.id, c.revision));
    EXPECT_FALSE(s.empty());
    EXPECT_TRUE(s.preedit().first.find("你") != std::string::npos);
    s.home();
    s.backspace();
    auto hello = find(s, "你好");
    EXPECT_EQ(s.select(hello.id, hello.revision), std::optional<std::string>("你好"));
}
TEST(Pinyin, StaleCandidateRejected) {
    Session s(base());
    s.type("nihao");
    auto c = find(s, "你好");
    s.type("ma");
    EXPECT_FALSE(s.select(c.id, c.revision));
    EXPECT_EQ(s.raw(), "nihaoma");
}
TEST(Pinyin, CursorEditing) {
    Session s(base());
    s.type("nihap");
    s.backspace();
    s.type("o");
    EXPECT_EQ(s.raw(), "nihao");
    find(s, "你好");
    s.move(-1);
    s.deleteForward();
    EXPECT_EQ(s.raw(), "niha");
    s.end();
    s.type("o");
    find(s, "你好");
}
TEST(Pinyin, SeparatorAndV) {
    Session s(base());
    s.type("xi'an");
    find(s, "西安");
    s.clear();
    s.type("lv");
    find(s, "绿");
}
TEST(Pinyin, LongInputBounded) {
    Session s(base());
    EXPECT_TRUE(s.type("nihao"));
    EXPECT_FALSE(s.type(std::string(256, 'a')));
    EXPECT_EQ(s.raw(), "nihao");
}
TEST(Pinyin, TraditionalOutput) {
    auto b = base();
    Session s(b);
    Options o;
    o.traditional = true;
    s.configure(o);
    s.type("hanyu");
    auto c = find(s, "漢語");
    EXPECT_EQ(s.select(c.id, c.revision), std::optional<std::string>("漢語"));
    ASSERT_EQ(s.learning().size(), 1);
    EXPECT_EQ(s.learning()[0].text, "汉语");
}
TEST(Pinyin, EmojiNotLearned) {
    Session s(base());
    s.type("xiaolian");
    auto c = find(s, "😊");
    EXPECT_EQ(s.select(c.id, c.revision), std::optional<std::string>("😊"));
    EXPECT_TRUE(s.learning().empty());
}
TEST(Pinyin, ExplicitCorrection) {
    Session s(base());
    s.type("nihap");
    auto c = find(s, "你好");
    EXPECT_FALSE(c.annotation.empty());
    EXPECT_EQ(s.select(c.id, c.revision), std::optional<std::string>("你好"));
}
TEST(Pinyin, AllShuangpinProfiles) {
    for (int profile = 0; profile < 3; ++profile) {
        auto b = base();
        Options o;
        o.shuangpin = true;
        o.profile = profile;
        b->configure(o);
        Session s(b);
        s.configure(o);
        s.type(profile == 1 ? "nihc" : "nihk");
        auto c = find(s, "你好");
        EXPECT_EQ(s.select(c.id, c.revision), std::optional<std::string>("你好"));
    }
}
TEST(Pinyin, FuzzyConfig) {
    auto b = base();
    Options o;
    o.zzh = true;
    b->configure(o);
    Session s(b);
    s.configure(o);
    s.type("zongguo");
    auto c = find(s, "中国");
    EXPECT_EQ(s.select(c.id, c.revision), std::optional<std::string>("中国"));
}
TEST(Pinyin, PersonalPhraseRecallAndRanking) {
    auto b = std::make_shared<Backend>(
        std::vector<Phrase>{{"gong chang chang shu", "工厂常数", 25, 100}});
    b->configure({});
    Session s(b);
    s.type("gongchangchangshu");
    auto c = find(s, "工厂常数");
    auto r = s.select(c.id, c.revision);
    EXPECT_EQ(r, std::optional<std::string>("工厂常数"));
}
TEST(Pinyin, IndependentContexts) {
    Session a(base()), b(base());
    a.type("nihao");
    b.type("zhongguo");
    EXPECT_EQ(a.raw(), "nihao");
    EXPECT_EQ(b.raw(), "zhongguo");
    a.clear();
    EXPECT_FALSE(b.empty());
}
TEST(Dictionary, ValidationAndAtomicImport) {
    auto dir = directory();
    auto path = dir / "terms.tsv";
    {
        std::ofstream o(path);
        o << "工厂常数\tgong chang chang shu\t100\n";
    }
    auto ps = readDictionary(path);
    ASSERT_EQ(ps.size(), 1);
    Database db(dir / "user.db");
    db.importDictionary("test", ps);
    ASSERT_EQ(db.phrases().size(), 1);
    {
        std::ofstream o(path);
        o << "错误\tni\t100\n";
    }
    EXPECT_THROW(readDictionary(path), std::runtime_error);
    EXPECT_EQ(db.phrases().size(), 1);
    db.enableDictionary("test", false);
    EXPECT_TRUE(db.phrases().empty());
    db.enableDictionary("test", true);
    EXPECT_EQ(db.phrases().size(), 1);
    db.removeDictionary("test");
    EXPECT_TRUE(db.phrases().empty());
}
TEST(Dictionary, RejectMalformed) {
    auto dir = directory();
    auto path = dir / "bad.tsv";
    for (const auto &line :
         {"word ni 100\n", "词\tabc\t2\n", "词\tci\t-1\n", "词\tci\t1\textra\n"}) {
        {
            std::ofstream o(path);
            o << line;
        }
        EXPECT_THROW(readDictionary(path), std::exception);
    }
}
TEST(Dictionary, BundledExamples) {
    EXPECT_EQ(readDictionary(std::string(NOVA_SOURCE_DIR) + "/data/semiconductor.tsv").size(), 5);
    EXPECT_EQ(readDictionary(std::string(NOVA_SOURCE_DIR) + "/data/programming.tsv").size(), 5);
    EXPECT_EQ(readDictionary(std::string(NOVA_SOURCE_DIR) + "/data/dictionaries/rime-common.tsv").size(),
              20000);
    EXPECT_EQ(readDictionary(std::string(NOVA_SOURCE_DIR) + "/data/dictionaries/rime-ice.tsv").size(),
              162);
}
TEST(Storage, PersistenceExportAndClear) {
    auto dir = directory();
    {
        Database db(dir / "user.db");
        for (int i = 0; i < 20; ++i)
            db.learn({"ni hao", "你好"});
        EXPECT_EQ(db.phrases().front().count, 20);
        db.exportUser(dir / "export.tsv");
        EXPECT_EQ(readDictionary(dir / "export.tsv").size(), 1);
    }
    {
        Database db(dir / "user.db");
        EXPECT_EQ(db.phrases().front().count, 20);
        auto generation = db.generation();
        db.clear();
        EXPECT_GT(db.generation(), generation);
        EXPECT_TRUE(db.phrases().empty());
    }
}
TEST(Storage, AsyncFlush) {
    auto dir = directory();
    {
        Store store(dir / "user.db");
        for (int retry = 0; retry < 100 && !store.snapshot()->clearEpoch; ++retry)
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        store.learn({"ni hao", "你好"});
        ASSERT_TRUE(store.flush());
    }
    Database db(dir / "user.db");
    ASSERT_EQ(db.phrases().size(), 1);
    EXPECT_EQ(db.phrases().front().text, "你好");
}
TEST(Storage, CorruptionPreserved) {
    auto dir = directory();
    {
        std::ofstream out(dir / "user.db");
        out << "corrupt database";
    }
    EXPECT_THROW(Database(dir / "user.db"), std::runtime_error);
    std::ifstream in(dir / "user.db");
    std::string s;
    std::getline(in, s);
    EXPECT_EQ(s, "corrupt database");
}
TEST(Storage, OldEpochCannotResurrectClearedData) {
    auto dir = directory();
    Database db(dir / "user.db");
    auto old = db.clearEpoch();
    db.learn({"ni hao", "你好"});
    db.clear();
    db.learnBatch({{{"ni hao", "你好"}, old}});
    EXPECT_TRUE(db.phrases().empty());
    db.learnBatch({{{"ni hao", "你好"}, db.clearEpoch()}});
    EXPECT_EQ(db.phrases().size(), 1);
}
TEST(Pinyin, RevisionUniqueAcrossReplacedSessions) {
    auto b = base();
    Session old(b);
    old.type("nihao");
    auto candidate = find(old, "你好");
    Session replacement(b);
    replacement.type("nihao");
    replacement.candidates();
    EXPECT_FALSE(replacement.select(candidate.id, candidate.revision));
}
TEST(Pinyin, SelectedCandidateMappingAfterReranking) {
    auto b = std::make_shared<Backend>(std::vector<Phrase>{{"ni hao", "拟好", 20, 0}});
    b->configure({});
    Session s(b);
    s.type("nihao");
    auto c = find(s, "拟好");
    auto result = s.select(c.id, c.revision);
    ASSERT_TRUE(result);
    EXPECT_EQ(*result, "拟好");
}
