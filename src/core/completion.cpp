// SPDX-License-Identifier: GPL-3.0-or-later
#include "completion.h"
#include "context.h"
#include <algorithm>
#include <map>
namespace nova {
void CompletionSession::changed() {
    revision_ = nextRevision();
    snapshot_.clear();
}
void CompletionSession::start(std::vector<ProjectTerm> terms) {
    clear();
    terms_ = std::move(terms);
    if (terms_.size() > 20000)
        terms_.resize(20000);
    std::stable_sort(terms_.begin(), terms_.end(), [](const auto &a, const auto &b) {
        return a.frequency != b.frequency ? a.frequency > b.frequency : a.text < b.text;
    });
    active_ = true;
}
void CompletionSession::clear() {
    active_ = false;
    raw_.clear();
    cursor_ = 0;
    terms_.clear();
    changed();
}
bool CompletionSession::type(std::string_view text) {
    if (!active_ || raw_.size() + text.size() > 128 ||
        std::any_of(text.begin(), text.end(), [](unsigned char c) { return c < 32 || c > 126; }))
        return false;
    raw_.insert(cursor_, text);
    cursor_ += text.size();
    changed();
    return true;
}
void CompletionSession::backspace() {
    if (cursor_)
        raw_.erase(--cursor_, 1);
    changed();
}
void CompletionSession::deleteForward() {
    if (cursor_ < raw_.size())
        raw_.erase(cursor_, 1);
    changed();
}
void CompletionSession::move(int delta) {
    cursor_ = std::clamp<int64_t>(int64_t(cursor_) + delta, 0, raw_.size());
    changed();
}
void CompletionSession::home() {
    cursor_ = 0;
    changed();
}
void CompletionSession::end() {
    cursor_ = raw_.size();
    changed();
}
std::vector<CompletionCandidate> CompletionSession::candidates() {
    if (!snapshot_.empty())
        return snapshot_;
    if (!active_ || raw_.empty())
        return {};
    struct Builtin {
        const char *prefix, *text, *kind;
    };
    static const Builtin builtins[] = {
        {"git checkout", "git checkout", "命令"},
        {"git cherry-pick", "git cherry-pick", "命令"},
        {"git commit", "git commit", "命令"},
        {"git status", "git status", "命令"},
        {"git switch", "git switch", "命令"},
        {"git log", "git log", "命令"},
        {"git diff", "git diff", "命令"},
        {"git branch", "git branch", "命令"},
        {"cmake target_link_libraries", "target_link_libraries", "CMake"},
        {"cmake target_include_directories", "target_include_directories", "CMake"},
        {"cmake target_compile_options", "target_compile_options", "CMake"},
        {"cmake add_executable", "add_executable", "CMake"},
        {"cmake add_library", "add_library", "CMake"},
        {"cmake find_package", "find_package", "CMake"},
        {"cmake --build", "cmake --build", "命令"},
        {"ctest --output-on-failure", "ctest --output-on-failure", "命令"}};
    std::map<std::string, std::string> found;
    for (const auto &b : builtins)
        if (std::string_view(b.prefix).starts_with(raw_))
            found.emplace(b.text, b.kind);
    for (const auto &p : terms_)
        if (p.text.starts_with(raw_) && !found.count(p.text) && snapshot_.size() < 40) {
            snapshot_.push_back({0, revision_, p.text, "项目标识符"});
            found.emplace(p.text, "");
        }
    for (const auto &[text, annotation] : found)
        if (!annotation.empty() && snapshot_.size() < 40)
            snapshot_.push_back({0, revision_, text, annotation});
    uint64_t id = 1;
    for (auto &c : snapshot_)
        c.id = id++;
    return snapshot_;
}
std::optional<std::string> CompletionSession::select(uint64_t id, uint64_t revision) {
    if (!active_ || revision != revision_)
        return std::nullopt;
    const auto i = std::find_if(snapshot_.begin(), snapshot_.end(),
                                [id](const auto &c) { return c.id == id; });
    if (i == snapshot_.end())
        return std::nullopt;
    auto text = i->text;
    clear();
    return text;
}
} // namespace nova
