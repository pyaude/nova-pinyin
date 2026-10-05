// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <memory>
namespace fcitx {
class Instance;
class InputMethodEngine;
} // namespace fcitx
namespace nova {
std::unique_ptr<fcitx::InputMethodEngine> createEngine(fcitx::Instance *instance);
}
