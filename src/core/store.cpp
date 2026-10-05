// SPDX-License-Identifier: GPL-3.0-or-later
#include "store.h"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fcitx-utils/utf8.h>
#include <fstream>
#include <libime/pinyin/pinyinencoder.h>
#include <map>
#include <sqlite3.h>
#include <sstream>
#include <stdexcept>
#include <sys/stat.h>
namespace nova {
namespace {
struct Statement {
    sqlite3_stmt *s = nullptr;
    Statement(sqlite3 *db, const char *sql) {
        if (sqlite3_prepare_v2(db, sql, -1, &s, nullptr) != SQLITE_OK)
            throw std::runtime_error("数据库语句准备失败");
    }
    ~Statement() { sqlite3_finalize(s); }
    void bind(int i, const std::string &v) {
        if (sqlite3_bind_text(s, i, v.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK)
            throw std::runtime_error("数据库参数失败");
    }
    void number(int i, int n) { sqlite3_bind_int(s, i, n); }
    void done() {
        if (sqlite3_step(s) != SQLITE_DONE)
            throw std::runtime_error("数据库写入失败（权限、锁或磁盘空间）");
    }
    std::string text(int i) {
        const auto *p = sqlite3_column_text(s, i);
        return p ? reinterpret_cast<const char *>(p) : "";
    }
};
void checkName(const std::string &name) {
    if (name.empty() || name.size() > 80 || !fcitx::utf8::validate(name) ||
        name.find_first_of("\r\n\t") != std::string::npos || name.find('\0') != std::string::npos)
        throw std::runtime_error("词库名称不合法");
}
bool validIdentifier(const std::string &text) {
    auto alpha = [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
    };
    return text.size() >= 3 && text.size() <= 64 && alpha(text.front()) &&
           std::all_of(text.begin(), text.end(),
                       [&](unsigned char c) { return alpha(c) || (c >= '0' && c <= '9'); });
}
} // namespace
std::filesystem::path dataHome() {
    const char *xdg = std::getenv("XDG_DATA_HOME");
    if (xdg && *xdg && std::filesystem::path(xdg).is_absolute())
        return std::filesystem::path(xdg) / "novapinyin";
    const char *home = std::getenv("HOME");
    if (!home || !*home)
        throw std::runtime_error("无法确定用户数据目录");
    return std::filesystem::path(home) / ".local/share/novapinyin";
}
std::string normalizeReading(std::string reading) {
    std::string result;
    for (unsigned char c : reading) {
        if (c == ' ' || c == '\'') {
            if (!result.empty() && result.back() != ' ')
                result += ' ';
        } else if (c >= 'a' && c <= 'z')
            result += char(c);
        else
            throw std::runtime_error("读音必须为小写无声调全拼，ü 使用 v");
    }
    if (!result.empty() && result.back() == ' ')
        result.pop_back();
    std::string encoded = result;
    std::replace(encoded.begin(), encoded.end(), ' ', '\'');
    if (result.empty() ||
        !libime::PinyinEncoder::isValidUserPinyin(libime::PinyinEncoder::encodeFullPinyin(encoded)))
        throw std::runtime_error("读音含非法音节");
    return result;
}
std::vector<Phrase> readDictionary(const std::filesystem::path &path) {
    if (std::filesystem::file_size(path) > 8 * 1024 * 1024)
        throw std::runtime_error("词库超过 8 MiB 限制");
    std::ifstream in(path);
    if (!in)
        throw std::runtime_error("无法打开词库");
    std::map<std::pair<std::string, std::string>, Phrase> unique;
    std::string line;
    size_t number = 0;
    while (std::getline(in, line)) {
        ++number;
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.empty() || line[0] == '#')
            continue;
        try {
            if (line.size() > 2048 || !fcitx::utf8::validate(line))
                throw std::runtime_error("行过长或不是 UTF-8");
            auto a = line.find('\t'), b = line.find('\t', a == std::string::npos ? a : a + 1);
            if (a == std::string::npos || b == std::string::npos ||
                line.find('\t', b + 1) != std::string::npos)
                throw std::runtime_error("需要三个制表符分隔字段");
            Phrase p;
            p.text = line.substr(0, a);
            p.reading = normalizeReading(line.substr(a + 1, b - a - 1));
            if (p.text.empty() || p.text.find_first_of("\r\n") != std::string::npos ||
                p.text.find('\0') != std::string::npos)
                throw std::runtime_error("词条内容非法");
            const auto w = line.substr(b + 1);
            size_t used = 0;
            p.weight = std::stoi(w, &used);
            if (used != w.size() || p.weight < 0 || p.weight > 100000)
                throw std::runtime_error("权重范围为 0..100000");
            auto reading = p.reading;
            std::replace(reading.begin(), reading.end(), ' ', '\'');
            if (libime::PinyinEncoder::encodeFullPinyin(reading).size() / 2 !=
                fcitx::utf8::length(p.text))
                throw std::runtime_error("汉字数与音节数不一致（emoji 不在词库格式内）");
            unique[{p.reading, p.text}] = p;
            if (unique.size() > 50000)
                throw std::runtime_error("词库超过 50000 条");
        } catch (const std::exception &e) {
            throw std::runtime_error("第 " + std::to_string(number) + " 行：" + e.what());
        }
    }
    std::vector<Phrase> result;
    for (auto &[key, p] : unique)
        result.push_back(std::move(p));
    return result;
}
std::vector<ProjectTerm> readProjectTerms(const std::filesystem::path &path) {
    if (std::filesystem::file_size(path) > 2 * 1024 * 1024)
        throw std::runtime_error("项目索引超过 2 MiB");
    std::ifstream in(path);
    if (!in)
        throw std::runtime_error("无法读取项目索引");
    std::map<std::string, int> terms;
    std::string line;
    size_t number = 0;
    while (std::getline(in, line)) {
        ++number;
        if (line.empty())
            continue;
        auto tab = line.find('\t');
        if (line.size() > 80 || tab == std::string::npos ||
            line.find('\t', tab + 1) != std::string::npos)
            throw std::runtime_error("项目索引第 " + std::to_string(number) + " 行格式错误");
        auto word = line.substr(0, tab), count = line.substr(tab + 1);
        size_t used = 0;
        int frequency = std::stoi(count, &used);
        if (!validIdentifier(word) || used != count.size() || frequency < 1 || frequency > 1000000)
            throw std::runtime_error("项目索引含非法标识符或频次");
        terms[word] = frequency;
        if (terms.size() > 20000)
            throw std::runtime_error("项目标识符超过 20000 条");
    }
    if (in.bad())
        throw std::runtime_error("项目索引读取失败");
    std::vector<ProjectTerm> result;
    for (const auto &[text, frequency] : terms)
        result.push_back({text, frequency});
    return result;
}
Database::Database(const std::filesystem::path &path) {
    std::filesystem::create_directories(path.parent_path());
    chmod(path.parent_path().c_str(), 0700);
    if (sqlite3_open_v2(path.c_str(), &db_, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) !=
        SQLITE_OK) {
        if (db_)
            sqlite3_close(db_);
        db_ = nullptr;
        throw std::runtime_error("无法打开学习数据库");
    }
    chmod(path.c_str(), 0600);
    sqlite3_busy_timeout(db_, 250);
    try {
        {
            Statement version(db_, "PRAGMA user_version");
            sqlite3_step(version.s);
            if (sqlite3_column_int(version.s, 0) > 2)
                throw std::runtime_error("数据库版本较新，请升级输入法");
        }
        exec("PRAGMA journal_mode=WAL; PRAGMA foreign_keys=ON;");
        exec("BEGIN IMMEDIATE; CREATE TABLE IF NOT EXISTS user_phrase(reading TEXT NOT NULL,phrase "
             "TEXT NOT "
             "NULL,selection_count INTEGER NOT NULL DEFAULT 0 CHECK(selection_count>=0),last_used "
             "INTEGER NOT NULL,PRIMARY KEY(reading,phrase));"
             "CREATE TABLE IF NOT EXISTS dictionary(name TEXT PRIMARY KEY,enabled INTEGER NOT NULL "
             "DEFAULT 1);"
             "CREATE TABLE IF NOT EXISTS dictionary_phrase(name TEXT NOT NULL REFERENCES "
             "dictionary(name) ON DELETE CASCADE,reading TEXT NOT NULL,phrase TEXT NOT NULL,weight "
             "INTEGER NOT NULL,PRIMARY KEY(name,reading,phrase));"
             "CREATE TABLE IF NOT EXISTS meta(id INTEGER PRIMARY KEY CHECK(id=1),generation "
             "INTEGER NOT NULL,clear_epoch INTEGER NOT NULL);"
             "INSERT OR IGNORE INTO meta VALUES(1,1,1);"
             "CREATE TABLE IF NOT EXISTS project(name TEXT PRIMARY KEY,root TEXT NOT NULL);"
             "CREATE TABLE IF NOT EXISTS project_term(name TEXT NOT NULL REFERENCES project(name) "
             "ON DELETE CASCADE,identifier TEXT NOT NULL,frequency INTEGER NOT NULL,PRIMARY "
             "KEY(name,identifier));"
             "PRAGMA user_version=2; COMMIT;");
    } catch (...) {
        sqlite3_close(db_);
        db_ = nullptr;
        throw;
    }
}
Database::~Database() {
    if (db_)
        sqlite3_close(db_);
}
void Database::exec(const char *sql) {
    if (sqlite3_exec(db_, sql, nullptr, nullptr, nullptr) != SQLITE_OK)
        throw std::runtime_error("数据库操作失败");
}
uint64_t Database::generation() {
    Statement q(db_, "SELECT generation FROM meta WHERE id=1");
    if (sqlite3_step(q.s) != SQLITE_ROW)
        throw std::runtime_error("数据库元数据读取失败");
    return sqlite3_column_int64(q.s, 0);
}
uint64_t Database::clearEpoch() {
    Statement q(db_, "SELECT clear_epoch FROM meta WHERE id=1");
    if (sqlite3_step(q.s) != SQLITE_ROW)
        throw std::runtime_error("数据库元数据读取失败");
    return sqlite3_column_int64(q.s, 0);
}
std::vector<Phrase> Database::phrases() {
    Statement q(db_, "SELECT reading,phrase,selection_count,0 FROM user_phrase UNION ALL SELECT "
                     "p.reading,p.phrase,0,p.weight FROM dictionary_phrase p JOIN dictionary d ON "
                     "d.name=p.name WHERE d.enabled=1");
    std::vector<Phrase> r;
    int status;
    while ((status = sqlite3_step(q.s)) == SQLITE_ROW) {
        if (r.size() >= 100000)
            throw std::runtime_error("已启用词条总数超过 100000，请禁用部分词库");
        r.push_back({q.text(0), q.text(1), sqlite3_column_int(q.s, 2), sqlite3_column_int(q.s, 3)});
    }
    if (status != SQLITE_DONE)
        throw std::runtime_error("数据库读取失败");
    return r;
}
void Database::learn(const Phrase &p) {
    Statement q(db_,
                "INSERT INTO user_phrase VALUES(?,?,1,unixepoch()) ON CONFLICT(reading,phrase) DO "
                "UPDATE SET selection_count=MIN(selection_count+1,1000000),last_used=unixepoch()");
    q.bind(1, p.reading);
    q.bind(2, p.text);
    q.done();
}
void Database::learnBatch(const std::vector<std::pair<Phrase, uint64_t>> &batch) {
    exec("BEGIN IMMEDIATE");
    try {
        auto current = clearEpoch();
        for (const auto &[phrase, epoch] : batch)
            if (!epoch || epoch == current)
                learn(phrase);
        exec("COMMIT");
    } catch (...) {
        exec("ROLLBACK");
        throw;
    }
}
void Database::clear() {
    exec("BEGIN IMMEDIATE; DELETE FROM user_phrase; DELETE FROM project; UPDATE meta SET "
         "generation=generation+1,clear_epoch=clear_epoch+1; "
         "COMMIT;");
}
void Database::importProject(const std::string &name, const std::string &root,
                             const std::vector<ProjectTerm> &terms, uint64_t expectedGeneration) {
    checkName(name);
    if (root.empty() || root.size() > 4096 || root.find_first_of("\r\n\t") != std::string::npos ||
        root.find('\0') != std::string::npos || !fcitx::utf8::validate(root) ||
        !std::filesystem::path(root).is_absolute())
        throw std::runtime_error("项目根目录必须为合法绝对路径");
    if (terms.size() > 20000)
        throw std::runtime_error("项目标识符超过 20000 条");
    for (const auto &term : terms)
        if (!validIdentifier(term.text) || term.frequency < 1 || term.frequency > 1000000)
            throw std::runtime_error("项目标识符不合法");
    exec("BEGIN IMMEDIATE");
    try {
        if (expectedGeneration && generation() != expectedGeneration)
            throw std::runtime_error("数据版本已改变，索引已取消，请重试");
        Statement p(
            db_,
            "INSERT INTO project VALUES(?,?) ON CONFLICT(name) DO UPDATE SET root=excluded.root");
        p.bind(1, name);
        p.bind(2, root);
        p.done();
        Statement del(db_, "DELETE FROM project_term WHERE name=?");
        del.bind(1, name);
        del.done();
        Statement ins(db_, "INSERT INTO project_term VALUES(?,?,?)");
        for (const auto &term : terms) {
            sqlite3_reset(ins.s);
            ins.bind(1, name);
            ins.bind(2, term.text);
            ins.number(3, term.frequency);
            ins.done();
        }
        Statement count(
            db_, "SELECT (SELECT COUNT(*) FROM project),(SELECT COUNT(*) FROM project_term)");
        if (sqlite3_step(count.s) != SQLITE_ROW || sqlite3_column_int(count.s, 0) > 10 ||
            sqlite3_column_int(count.s, 1) > 100000)
            throw std::runtime_error("项目数超过 10 或总标识符超过 100000");
        exec("UPDATE meta SET generation=generation+1; COMMIT;");
    } catch (...) {
        exec("ROLLBACK");
        throw;
    }
}
std::vector<Project> Database::projects() {
    Statement q(db_, "SELECT p.name,p.root,t.identifier,t.frequency FROM project p LEFT JOIN "
                     "project_term t ON p.name=t.name ORDER BY p.name,t.identifier");
    std::vector<Project> result;
    int status;
    size_t total = 0;
    while ((status = sqlite3_step(q.s)) == SQLITE_ROW) {
        if (result.empty() || result.back().name != q.text(0))
            result.push_back({q.text(0), q.text(1), {}});
        if (sqlite3_column_type(q.s, 2) != SQLITE_NULL) {
            if (++total > 100000)
                throw std::runtime_error("项目索引过大");
            result.back().terms.push_back({q.text(2), sqlite3_column_int(q.s, 3)});
        }
    }
    if (status != SQLITE_DONE)
        throw std::runtime_error("项目索引读取失败");
    return result;
}
void Database::removeProject(const std::string &name) {
    checkName(name);
    exec("BEGIN IMMEDIATE");
    try {
        Statement q(db_, "DELETE FROM project WHERE name=?");
        q.bind(1, name);
        q.done();
        exec("UPDATE meta SET generation=generation+1; COMMIT;");
    } catch (...) {
        exec("ROLLBACK");
        throw;
    }
}
void Database::importDictionary(const std::string &name, const std::vector<Phrase> &ps) {
    checkName(name);
    exec("BEGIN IMMEDIATE");
    try {
        Statement d(db_, "INSERT OR IGNORE INTO dictionary VALUES(?,1)");
        d.bind(1, name);
        d.done();
        Statement del(db_, "DELETE FROM dictionary_phrase WHERE name=?");
        del.bind(1, name);
        del.done();
        Statement ins(db_, "INSERT INTO dictionary_phrase VALUES(?,?,?,?)");
        for (const auto &p : ps) {
            sqlite3_reset(ins.s);
            ins.bind(1, name);
            ins.bind(2, p.reading);
            ins.bind(3, p.text);
            ins.number(4, p.weight);
            ins.done();
        }
        exec("UPDATE meta SET generation=generation+1; COMMIT;");
    } catch (...) {
        exec("ROLLBACK");
        throw;
    }
}
void Database::enableDictionary(const std::string &name, bool enabled) {
    checkName(name);
    exec("BEGIN IMMEDIATE");
    try {
        Statement q(db_, "UPDATE dictionary SET enabled=? WHERE name=?");
        q.number(1, enabled);
        q.bind(2, name);
        q.done();
        exec("UPDATE meta SET generation=generation+1; COMMIT;");
    } catch (...) {
        exec("ROLLBACK");
        throw;
    }
}
void Database::removeDictionary(const std::string &name) {
    checkName(name);
    exec("BEGIN IMMEDIATE");
    try {
        Statement q(db_, "DELETE FROM dictionary WHERE name=?");
        q.bind(1, name);
        q.done();
        exec("UPDATE meta SET generation=generation+1; COMMIT;");
    } catch (...) {
        exec("ROLLBACK");
        throw;
    }
}
std::vector<std::pair<std::string, bool>> Database::dictionaries() {
    Statement q(db_, "SELECT name,enabled FROM dictionary ORDER BY name");
    std::vector<std::pair<std::string, bool>> r;
    int status;
    while ((status = sqlite3_step(q.s)) == SQLITE_ROW)
        r.emplace_back(q.text(0), sqlite3_column_int(q.s, 1));
    if (status != SQLITE_DONE)
        throw std::runtime_error("词库列表读取失败");
    return r;
}
void Database::exportUser(const std::filesystem::path &path) {
    auto temporary = path;
    temporary += ".tmp";
    std::ofstream out(temporary);
    if (!out)
        throw std::runtime_error("无法写入导出文件");
    chmod(temporary.c_str(), 0600);
    out << "# novapinyin-dict-v1\n";
    Statement q(db_,
                "SELECT phrase,reading,selection_count FROM user_phrase ORDER BY reading,phrase");
    int status;
    while ((status = sqlite3_step(q.s)) == SQLITE_ROW)
        out << q.text(0) << '\t' << q.text(1) << '\t'
            << std::min(100000, sqlite3_column_int(q.s, 2)) << '\n';
    if (status != SQLITE_DONE)
        throw std::runtime_error("个人词条读取失败，导出未完成");
    out.flush();
    if (!out)
        throw std::runtime_error("导出写入失败");
    out.close();
    std::filesystem::rename(temporary, path);
}
Store::Store(std::filesystem::path path)
    : path_(std::move(path)), snapshot_(std::make_shared<Snapshot>()), worker_([this] { run(); }) {}
Store::~Store() {
    {
        std::lock_guard lock(mutex_);
        stopping_ = true;
    }
    cv_.notify_all();
    worker_.join();
}
std::shared_ptr<const Snapshot> Store::snapshot() const {
    std::lock_guard lock(mutex_);
    return snapshot_;
}
void Store::learn(Phrase p) {
    std::lock_guard lock(mutex_);
    if (!snapshot_->clearEpoch)
        throw std::runtime_error("个人学习数据尚未就绪");
    if (queue_.size() >= 4096)
        throw std::runtime_error("学习队列已满");
    queue_.emplace_back(std::move(p), snapshot_->clearEpoch);
    cv_.notify_all();
}
bool Store::flush(std::chrono::milliseconds timeout) {
    std::unique_lock lock(mutex_);
    return cv_.wait_for(lock, timeout, [this] { return queue_.empty() && !busy_; });
}
void Store::run() {
    std::unique_ptr<Database> db;
    uint64_t epoch = 0;
    for (;;) {
        std::deque<std::pair<Phrase, uint64_t>> batch;
        {
            std::unique_lock lock(mutex_);
            cv_.wait_for(lock, std::chrono::milliseconds(200),
                         [this] { return stopping_ || !queue_.empty(); });
            if (stopping_ && queue_.empty())
                break;
            busy_ = true;
            batch.swap(queue_);
        }
        try {
            if (!db)
                db = std::make_unique<Database>(path_);
            auto generation = db->generation();
            bool changed = generation != epoch;
            epoch = generation;
            if (!batch.empty())
                db->learnBatch({batch.begin(), batch.end()});
            if (!batch.empty() || changed) {
                auto next = std::make_shared<Snapshot>();
                next->generation = generation;
                next->clearEpoch = db->clearEpoch();
                next->phrases = db->phrases();
                next->projects = db->projects();
                {
                    std::lock_guard lock(mutex_);
                    snapshot_ = std::move(next);
                }
            }
        } catch (const std::exception &e) {
            auto next = std::make_shared<Snapshot>();
            next->error = e.what();
            {
                std::lock_guard lock(mutex_);
                snapshot_ = std::move(next);
            }
            db.reset();
            epoch = 0;
        }
        {
            std::lock_guard lock(mutex_);
            busy_ = false;
        }
        cv_.notify_all();
    }
}
} // namespace nova
