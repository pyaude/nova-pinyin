// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <string>
#include <string_view>
namespace nova {
uint64_t nextRevision();
// Recent text belongs to ONE input context. It is never written to storage.
class ContextBuffer {
  public:
    static constexpr size_t limit = 128;
    void clear() { text_.clear(); }
    void committed(const std::string &text);
    bool surrounding(const std::string &text, size_t cursor, size_t anchor);
    const std::string &text() const { return text_; }

  private:
    std::string text_;
};
enum class ApplicationClass { General, Terminal, Editor };
ApplicationClass classifyApplication(std::string program, bool terminal,
                                     const std::string &overrides);
} // namespace nova
