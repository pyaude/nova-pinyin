// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "store.h"
#include <optional>
#include <string>
#include <vector>
namespace nova {
struct CompletionCandidate {
    uint64_t id, revision;
    std::string text, annotation;
};
class CompletionSession {
  public:
    void start(std::vector<ProjectTerm> terms = {});
    void clear();
    bool type(std::string_view text);
    void backspace();
    void deleteForward();
    void move(int delta);
    void home();
    void end();
    const std::string &raw() const { return raw_; }
    size_t cursor() const { return cursor_; }
    bool active() const { return active_; }
    std::vector<CompletionCandidate> candidates();
    std::optional<std::string> select(uint64_t id, uint64_t revision);

  private:
    bool active_ = false;
    size_t cursor_ = 0;
    uint64_t revision_ = 0;
    std::string raw_;
    std::vector<ProjectTerm> terms_;
    std::vector<CompletionCandidate> snapshot_;
    void changed();
};
} // namespace nova
