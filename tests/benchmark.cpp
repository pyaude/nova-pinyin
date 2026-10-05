// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/core.h"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <sys/resource.h>
int main() {
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
    std::cout << "startup_ms=" << startup << " samples=" << times.size()
              << " p50_ms=" << percentile(.50) << " p95_ms=" << percentile(.95)
              << " p99_ms=" << percentile(.99) << " max_ms=" << times.back()
              << " peak_rss_kib=" << usage.ru_maxrss << " max_candidates=" << maxCandidates << '\n';
}
