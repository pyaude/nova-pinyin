// SPDX-License-Identifier: GPL-3.0-or-later
#include "context.h"
#include <algorithm>
#include <atomic>
#include <fcitx-utils/utf8.h>
#include <sstream>
namespace nova {
uint64_t nextRevision() {
    static std::atomic<uint64_t> revision{1};
    return revision.fetch_add(1);
}
void ContextBuffer::committed(const std::string &text) {
    if (text.size() > 65536 || !fcitx::utf8::validate(text)) {
        clear();
        return;
    }
    auto joined = text_ + text;
    const auto length = fcitx::utf8::length(joined);
    text_ = length > limit
                ? joined.substr(fcitx::utf8::ncharByteLength(joined.begin(), length - limit))
                : std::move(joined);
}
bool ContextBuffer::surrounding(const std::string &text, size_t cursor, size_t anchor) {
    clear();
    if (text.size() > 65536 || !fcitx::utf8::validate(text) || cursor != anchor ||
        cursor > fcitx::utf8::length(text))
        return false;
    if (!cursor)
        return true;
    const auto start = cursor > limit ? cursor - limit : 0;
    const auto beginByte = fcitx::utf8::ncharByteLength(text.begin(), start);
    const auto endByte = fcitx::utf8::ncharByteLength(text.begin(), cursor);
    text_ = text.substr(beginByte, endByte - beginByte);
    return true;
}
ApplicationClass classifyApplication(std::string program, bool terminal,
                                     const std::string &overrides) {
    if (program.size() > 256)
        program.clear();
    auto slash = program.find_last_of('/');
    if (slash != std::string::npos)
        program.erase(0, slash + 1);
    auto lower = [](std::string value) {
        for (char &c : value)
            if (c >= 'A' && c <= 'Z')
                c += 'a' - 'A';
        return value;
    };
    program = lower(program);
    std::istringstream stream(overrides.substr(0, 4096));
    std::string rule;
    while (std::getline(stream, rule, ';')) {
        const auto equal = rule.find('=');
        if (equal != std::string::npos && !program.empty() &&
            lower(rule.substr(0, equal)) == program) {
            auto type = lower(rule.substr(equal + 1));
            if (type == "general")
                return ApplicationClass::General;
            if (type == "terminal")
                return ApplicationClass::Terminal;
            if (type == "editor")
                return ApplicationClass::Editor;
        }
    }
    if (terminal || program == "gnome-terminal" || program == "org.gnome.terminal" ||
        program == "konsole" || program == "kitty" || program == "alacritty" ||
        program == "xterm" || program == "wezterm")
        return ApplicationClass::Terminal;
    if (program == "code" || program == "codium" || program == "gedit" || program == "kate" ||
        program == "org.gnome.texteditor" || program == "sublime_text" || program == "nvim")
        return ApplicationClass::Editor;
    return ApplicationClass::General;
}
} // namespace nova
