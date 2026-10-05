// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/completion.h"
#include "core/core.h"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <sys/resource.h>
int main(int argc, char **argv) {
    const bool context = argc == 2 && std::string(argv[1]) == "--context";
    const bool completion = argc == 2 && std::string(argv[1]) == "--completion";
    if (completion) {
        std::vector<nova::ProjectTerm> terms;
        for (int i = 0; i < 20000; ++i)
            terms.push_back({"ProcessSymbol" + std::to_string(i), i + 1});
        std::vector<double> times;
        double maxStart = 0;
        for (int round = 0; round < 30; ++round) {
            nova::CompletionSession s;
            auto start = std::chrono::steady_clock::now();
            s.start(terms);
            maxStart = std::max(maxStart, std::chrono::duration<double, std::milli>(
                                              std::chrono::steady_clock::now() - start)
                                              .count());
            for (char c : std::string("ProcessSym")) {
                auto begin = std::chrono::steady_clock::now();
                s.type(std::string(1, c));
                s.candidates();
                times.push_back(std::chrono::duration<double, std::milli>(
                                    std::chrono::steady_clock::now() - begin)
                                    .count());
            }
        }
        std::sort(times.begin(), times.end());
        std::cout << "completion_terms=20000 samples=" << times.size()
                  << " p95_ms=" << times[times.size() * 95 / 100] << " max_ms=" << times.back()
                  << " max_start_ms=" << maxStart << '\n';
        return 0;
    }
    auto start = std::chrono::steady_clock::now();
    auto b = std::make_shared<nova::Backend>();
    b->configure({});
    double startup =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    std::vector<double> times;
    size_t maxCandidates = 0;
    for (int round = 0; round < 30; ++round)
        for (const auto &raw :
             {"nihao", "zhongguo", "woaizhongguo", "gongchangchangshu", "xi'an", "lv"}) {
            nova::Session session(b);
            session.configure({});
            if (context)
                session.setContext("我正在银行办理业务，需要");
            for (char c : std::string(raw)) {
                auto begin = std::chrono::steady_clock::now();
                session.type(std::string(1, c));
                auto candidates = session.candidates();
                auto preedit = session.preedit();
                double elapsed = std::chrono::duration<double, std::milli>(
                                     std::chrono::steady_clock::now() - begin)
                                     .count();
                times.push_back(elapsed);
                maxCandidates = std::max(maxCandidates, candidates.size());
            }
        }
    rusage usage{};
    getrusage(RUSAGE_SELF, &usage);
    std::sort(times.begin(), times.end());
    auto percentile = [&](double p) {
        return times.at(std::min(times.size() - 1, size_t(times.size() * p)));
    };
    std::cout << "context_enabled=" << context << " startup_ms=" << startup
              << " samples=" << times.size() << " p50_ms=" << percentile(.50)
              << " p95_ms=" << percentile(.95) << " p99_ms=" << percentile(.99)
              << " max_ms=" << times.back() << " peak_rss_kib=" << usage.ru_maxrss
              << " max_candidates=" << maxCandidates << '\n';
}
