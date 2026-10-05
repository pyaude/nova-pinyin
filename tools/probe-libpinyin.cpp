// SPDX-License-Identifier: GPL-3.0-or-later
// Optional Phase 0 probe; not installed and not a production backend.
#include <chrono>
#include <filesystem>
#include <iostream>
#include <pinyin.h>
#include <unistd.h>
int main(int argc, char **argv) {
    if (argc != 2) {
        std::cerr << "Usage: probe-libpinyin /usr/lib/<architecture>/libpinyin/data\n";
        return 1;
    }
    auto user = std::filesystem::temp_directory_path() /
                ("nova-libpinyin-probe-" + std::to_string(getpid()));
    std::filesystem::create_directories(user);
    auto *context = pinyin_init(argv[1], user.c_str());
    if (!context)
        return 2;
    pinyin_load_phrase_library(context, 1);
    auto *instance = pinyin_alloc_instance(context);
    const auto begin = std::chrono::steady_clock::now();
    auto length = pinyin_parse_more_full_pinyins(instance, "nihao");
    pinyin_guess_sentence(instance);
    pinyin_guess_candidates(instance, 0, SORT_BY_PHRASE_LENGTH_AND_FREQUENCY);
    guint count = 0;
    pinyin_get_n_candidate(instance, &count);
    bool found = false;
    for (guint i = 0; i < count; ++i) {
        lookup_candidate_t *c = nullptr;
        const gchar *text = nullptr;
        pinyin_get_candidate(instance, i, &c);
        pinyin_get_candidate_string(instance, c, &text);
        if (text && std::string(text) == "你好")
            found = true;
    }
    std::cout << "backend=libpinyin parsed=" << length << " candidates=" << count
              << " contains_nihao=" << found << " elapsed_ms="
              << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin)
                     .count()
              << '\n';
    pinyin_free_instance(instance);
    pinyin_fini(context);
    std::filesystem::remove_all(user);
    return found ? 0 : 3;
}
