// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
struct sqlite3;
namespace nova {
struct Phrase {
    std::string reading, text;
    int count = 0;
    int weight = 0;
};
struct ProjectTerm {
    std::string text;
    int frequency = 1;
};
struct Project {
    std::string name, root;
    std::vector<ProjectTerm> terms;
};
struct Snapshot {
    uint64_t generation = 0;
    uint64_t clearEpoch = 0;
    std::vector<Phrase> phrases;
    std::vector<Project> projects;
    std::string error;
};
std::filesystem::path dataHome();
std::vector<Phrase> readDictionary(const std::filesystem::path &path);
std::string normalizeReading(std::string reading);
std::vector<ProjectTerm> readProjectTerms(const std::filesystem::path &path);
class Database {
  public:
    explicit Database(const std::filesystem::path &path);
    ~Database();
    Database(const Database &) = delete;
    Database &operator=(const Database &) = delete;
    std::vector<Phrase> phrases();
    void learn(const Phrase &phrase);
    void learnBatch(const std::vector<std::pair<Phrase, uint64_t>> &batch);
    void clear();
    void importDictionary(const std::string &name, const std::vector<Phrase> &phrases);
    void enableDictionary(const std::string &name, bool enabled);
    void removeDictionary(const std::string &name);
    std::vector<std::pair<std::string, bool>> dictionaries();
    void exportUser(const std::filesystem::path &path);
    uint64_t generation();
    uint64_t clearEpoch();
    void importProject(const std::string &name, const std::string &root,
                       const std::vector<ProjectTerm> &terms, uint64_t expectedGeneration = 0);
    std::vector<Project> projects();
    void removeProject(const std::string &name);

  private:
    sqlite3 *db_ = nullptr;
    void exec(const char *sql);
};
class Store {
  public:
    explicit Store(std::filesystem::path path);
    ~Store();
    void learn(Phrase phrase);
    std::shared_ptr<const Snapshot> snapshot() const;
    bool flush(std::chrono::milliseconds timeout = std::chrono::seconds(5));

  private:
    std::filesystem::path path_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<std::pair<Phrase, uint64_t>> queue_;
    std::shared_ptr<const Snapshot> snapshot_;
    bool stopping_ = false, busy_ = false;
    std::thread worker_;
    void run();
};
} // namespace nova
