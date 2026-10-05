// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "store.h"
#include <libime/pinyin/pinyincontext.h>
#include <libime/pinyin/pinyinime.h>
#include <map>
#include <memory>
#include <optional>
#include <string>
namespace nova {
struct Options {
    bool shuangpin = false, typo = true, traditional = false, emoji = true;
    int profile = 0;
    bool zzh = false, cch = false, ssh = false, nl = false, anang = false, eneng = false,
         ining = false;
};
struct Candidate {
    uint64_t id = 0, revision = 0;
    std::string text, reading, annotation;
    size_t backendIndex = 0, end = 0;
    int correction = -1;
    bool emoji = false;
};
class Backend {
  public:
    explicit Backend(const std::vector<Phrase> &phrases = {});
    ~Backend();
    void configure(const Options &options);
    std::string display(const std::string &text, bool traditional) const;
    libime::PinyinIME *ime() { return ime_.get(); }
    int frequency(const std::string &reading, const std::string &text) const;

  private:
    std::unique_ptr<libime::PinyinIME> ime_;
    std::map<std::pair<std::string, std::string>, int> counts_;
    struct Conversion;
    std::unique_ptr<Conversion> conversion_;
};
class Session {
  public:
    explicit Session(std::shared_ptr<Backend> backend);
    void configure(Options options);
    bool type(std::string_view text);
    void backspace();
    void deleteForward();
    void move(int delta);
    void home();
    void end();
    void clear();
    bool empty() const;
    std::string raw() const;
    std::string rawCommit() const;
    std::pair<std::string, size_t> preedit() const;
    std::vector<Candidate> candidates();
    std::optional<std::string> select(uint64_t id, uint64_t revision);
    const std::vector<Phrase> &learning() const { return learning_; }
    uint64_t revision() const { return revision_; }

  private:
    std::shared_ptr<Backend> backend_;
    libime::PinyinContext context_;
    Options options_;
    uint64_t revision_ = 1;
    std::vector<Candidate> snapshot_;
    std::vector<std::unique_ptr<libime::PinyinContext>> corrections_;
    std::vector<Phrase> learning_;
    void changed();
};
} // namespace nova
