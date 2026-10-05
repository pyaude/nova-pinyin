// SPDX-License-Identifier: GPL-3.0-or-later
#include "core.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <filesystem>
#include <libime/core/lattice.h>
#include <libime/core/userlanguagemodel.h>
#include <libime/pinyin/pinyindictionary.h>
#include <libime/pinyin/shuangpinprofile.h>
#include <opencc/SimpleConverter.hpp>
#include <set>
#include <stdexcept>
namespace nova {
namespace {
std::atomic<uint64_t> revisionCounter{1};
std::string compact(std::string s) {
    s.erase(std::remove_if(s.begin(), s.end(), [](char c) { return c == ' ' || c == '\''; }),
            s.end());
    return s;
}
std::string quoted(std::string s) {
    std::replace(s.begin(), s.end(), ' ', '\'');
    return s;
}
std::string modelPath(const char *file) {
    for (const char *dir : {"/usr/share/libime", "/usr/share/fcitx5/data"}) {
        auto p = std::filesystem::path(dir) / file;
        if (std::filesystem::exists(p))
            return p;
    }
    throw std::runtime_error("缺少 LibIME 数据包（libime-data）");
}
} // namespace
struct Backend::Conversion {
    opencc::SimpleConverter s2t{"s2t.json"};
};
Backend::Backend(const std::vector<Phrase> &phrases) {
    auto dict = std::make_unique<libime::PinyinDictionary>();
    dict->load(libime::PinyinDictionary::SystemDict, modelPath("sc.dict").c_str(),
               libime::PinyinDictFormat::Binary);
    std::map<std::pair<std::string, std::string>, Phrase> merged;
    for (const auto &p : phrases) {
        auto &entry = merged[{p.reading, p.text}];
        entry.reading = p.reading;
        entry.text = p.text;
        entry.count += p.count;
        entry.weight = std::max(entry.weight, p.weight);
    }
    for (const auto &[key, p] : merged) {
        auto reading = quoted(normalizeReading(p.reading));
        float cost = std::min(2.5, std::log1p(std::max(0, p.count)) * 0.6 +
                                       std::log1p(std::max(0, p.weight)) * 0.1);
        dict->addWord(libime::PinyinDictionary::UserDict, reading, p.text, cost);
        counts_[{p.reading, p.text}] += p.count;
    }
    ime_ = std::make_unique<libime::PinyinIME>(
        std::move(dict), std::make_unique<libime::UserLanguageModel>(NOVA_MODEL_FILE));
    ime_->setNBest(3);
    ime_->setBeamSize(32);
    ime_->setFrameSize(64);
    conversion_ = std::make_unique<Conversion>();
}
Backend::~Backend() = default;
void Backend::configure(const Options &o) {
    libime::PinyinFuzzyFlags flags = libime::PinyinFuzzyFlag::Inner;
    if (o.typo)
        flags |= libime::PinyinFuzzyFlag::CommonTypo;
    if (o.zzh)
        flags |= libime::PinyinFuzzyFlag::Z_ZH;
    if (o.cch)
        flags |= libime::PinyinFuzzyFlag::C_CH;
    if (o.ssh)
        flags |= libime::PinyinFuzzyFlag::S_SH;
    if (o.nl)
        flags |= libime::PinyinFuzzyFlag::L_N;
    if (o.anang)
        flags |= libime::PinyinFuzzyFlag::AN_ANG;
    if (o.eneng)
        flags |= libime::PinyinFuzzyFlag::EN_ENG;
    if (o.ining)
        flags |= libime::PinyinFuzzyFlag::IN_ING;
    ime_->setFuzzyFlags(flags);
    static const libime::ShuangpinBuiltinProfile profiles[] = {
        libime::ShuangpinBuiltinProfile::Ziranma, libime::ShuangpinBuiltinProfile::Xiaohe,
        libime::ShuangpinBuiltinProfile::MS};
    ime_->setShuangpinProfile(
        std::make_shared<libime::ShuangpinProfile>(profiles[std::clamp(o.profile, 0, 2)]));
}
std::string Backend::display(const std::string &s, bool t) const {
    return t ? conversion_->s2t.Convert(s) : s;
}
int Backend::frequency(const std::string &r, const std::string &t) const {
    auto i = counts_.find({r, t});
    return i == counts_.end() ? 0 : i->second;
}
Session::Session(std::shared_ptr<Backend> backend)
    : backend_(std::move(backend)), context_(backend_->ime()) {
    revision_ = revisionCounter.fetch_add(1);
    context_.setMaxSentenceLength(128);
}
void Session::configure(Options o) {
    options_ = o;
    context_.setUseShuangpin(o.shuangpin);
    clear();
}
void Session::changed() {
    revision_ = revisionCounter.fetch_add(1);
    snapshot_.clear();
    corrections_.clear();
    learning_.clear();
}
bool Session::type(std::string_view s) {
    if (context_.size() + s.size() > 256)
        return false;
    bool ok = context_.type(s);
    changed();
    return ok;
}
void Session::clear() {
    context_.clear();
    changed();
}
bool Session::empty() const { return context_.empty(); }
std::string Session::raw() const { return context_.userInput(); }
std::string Session::rawCommit() const {
    return context_.selectedSentence() + context_.userInput().substr(context_.selectedLength());
}
std::pair<std::string, size_t> Session::preedit() const {
    return context_.preeditWithCursor(libime::PinyinPreeditMode::RawText);
}
void Session::backspace() {
    if (context_.cursor() <= context_.selectedLength()) {
        context_.cancel();
    } else
        context_.erase(context_.cursor() - 1, context_.cursor());
    changed();
}
void Session::deleteForward() {
    if (context_.cursor() < context_.size())
        context_.erase(context_.cursor(), context_.cursor() + 1);
    changed();
}
void Session::move(int d) {
    auto pos = static_cast<int64_t>(context_.cursor()) + d;
    context_.setCursor(std::clamp<int64_t>(pos, 0, context_.size()));
    changed();
}
void Session::home() {
    context_.setCursor(0);
    changed();
}
void Session::end() {
    context_.setCursor(context_.size());
    changed();
}
std::vector<Candidate> Session::candidates() {
    if (!snapshot_.empty())
        return snapshot_;
    const auto &candidates = context_.candidates();
    size_t i = 0;
    std::set<std::pair<std::string, size_t>> seen;
    bool complete = false;
    for (const auto &c : candidates) {
        if (snapshot_.size() >= 180)
            break;
        auto text = c.toString();
        auto end =
            c.sentence().empty() ? context_.selectedLength() : c.sentence().back()->to()->index();
        auto reading = context_.candidateFullPinyin(i);
        std::replace(reading.begin(), reading.end(), '\'', ' ');
        complete |=
            end == context_.size() &&
            compact(reading) == compact(context_.userInput().substr(context_.selectedLength()));
        if (seen.emplace(text, end).second) {
            Candidate n;
            n.backendIndex = i;
            n.end = end;
            n.reading = reading;
            n.text = backend_->display(text, options_.traditional);
            if (!options_.shuangpin && compact(reading) != compact(context_.userInput().substr(
                                                               context_.selectedLength(),
                                                               end - context_.selectedLength())))
                n.annotation = "近似拼音";
            snapshot_.push_back(std::move(n));
        }
        ++i;
    }
    std::stable_sort(
        snapshot_.begin(), snapshot_.end(), [this](const Candidate &a, const Candidate &b) {
            // Do not boost a partial word over a full sentence just because it is frequent.
            if (a.end != b.end)
                return a.end > b.end;
            auto boost = [this](const Candidate &c) {
                const auto &original = context_.candidates()[c.backendIndex];
                return -double(c.backendIndex) * 0.1 +
                       std::min(4.0,
                                std::log1p(backend_->frequency(c.reading, original.toString())));
            };
            return boost(a) > boost(b);
        });
    if (options_.typo && !options_.shuangpin && !complete && context_.selectedLength() == 0 &&
        context_.size() >= 3 && context_.size() <= 32) {
        const auto raw = context_.userInput();
        const std::string rows[] = {"qwertyuiop", "asdfghjkl", "zxcvbnm"};
        std::vector<std::string> probes;
        for (const auto &row : rows) {
            auto pos = row.find(raw.back());
            if (pos != std::string::npos) {
                if (pos > 0) {
                    auto s = raw;
                    s.back() = row[pos - 1];
                    probes.push_back(s);
                }
                if (pos + 1 < row.size()) {
                    auto s = raw;
                    s.back() = row[pos + 1];
                    probes.push_back(s);
                }
            }
        }
        if (raw.size() >= 2) {
            auto s = raw;
            std::swap(s[s.size() - 1], s[s.size() - 2]);
            probes.push_back(s);
        }
        size_t insertion = std::min<size_t>(3, snapshot_.size());
        for (const auto &probe : probes) {
            auto ctx = std::make_unique<libime::PinyinContext>(backend_->ime());
            ctx->type(probe);
            const auto &v = ctx->candidates();
            for (size_t j = 0; j < std::min<size_t>(v.size(), 3); ++j) {
                if (v[j].sentence().empty() ||
                    v[j].sentence().back()->to()->index() != probe.size())
                    continue;
                if (compact(ctx->candidateFullPinyin(j)) != compact(probe))
                    continue;
                if (!seen.emplace(v[j].toString(), raw.size()).second)
                    continue;
                Candidate c;
                c.backendIndex = j;
                c.end = raw.size();
                c.correction = corrections_.size();
                c.text = backend_->display(v[j].toString(), options_.traditional);
                c.reading = ctx->candidateFullPinyin(j);
                std::replace(c.reading.begin(), c.reading.end(), '\'', ' ');
                c.annotation = "纠错：" + probe;
                snapshot_.insert(snapshot_.begin() + insertion++, std::move(c));
            }
            corrections_.push_back(std::move(ctx));
        }
    }
    if (options_.emoji && !options_.shuangpin && context_.selectedLength() == 0) {
        static const std::map<std::string, std::string> emoji = {
            {"xiaolian", "😊"}, {"zan", "👍"},     {"aixin", "❤️"}, {"daxiao", "😄"},
            {"qingzhu", "🎉"},  {"huojian", "🚀"}, {"ganxie", "🙏"},     {"jiayou", "💪"}};
        auto e = emoji.find(compact(context_.userInput()));
        if (e != emoji.end()) {
            Candidate c;
            c.emoji = true;
            c.text = e->second;
            c.end = context_.size();
            c.annotation = "表情";
            snapshot_.insert(snapshot_.begin() + std::min<size_t>(3, snapshot_.size()),
                             std::move(c));
        }
    }
    uint64_t id = 1;
    for (auto &c : snapshot_) {
        c.id = id++;
        c.revision = revision_;
    }
    return snapshot_;
}
std::optional<std::string> Session::select(uint64_t id, uint64_t revision) {
    if (revision != revision_)
        return std::nullopt;
    auto i = std::find_if(snapshot_.begin(), snapshot_.end(),
                          [id](const Candidate &c) { return c.id == id; });
    if (i == snapshot_.end())
        return std::nullopt;
    const auto c = *i;
    if (c.emoji) {
        auto result = c.text;
        clear();
        return result;
    }
    libime::PinyinContext *ctx = &context_;
    if (c.correction >= 0)
        ctx = corrections_.at(c.correction).get();
    ctx->select(c.backendIndex);
    if (ctx->selected()) {
        const auto text = ctx->selectedSentence(), reading = ctx->selectedFullPinyin();
        auto display = backend_->display(text, options_.traditional);
        std::string normalized = reading;
        std::replace(normalized.begin(), normalized.end(), '\'', ' ');
        clear();
        try {
            learning_.push_back({normalizeReading(normalized), text, 1, 0});
        } catch (const std::exception &) {
        }
        return display;
    }
    changed();
    return std::nullopt;
}
} // namespace nova
