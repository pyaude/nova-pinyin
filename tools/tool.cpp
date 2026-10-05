// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/core.h"
#include "core/store.h"
#include <fstream>
#include <iostream>
int main(int argc, char **argv) {
    try {
        if (argc < 2)
            throw std::runtime_error(
                "用法：novapinyin-tool list | import 名称 文件.tsv | export 文件.tsv | enable 名称 "
                "| disable 名称 | remove 名称 | clear --yes | validate 文件.tsv | query 拼音 | "
                "projects | project-import 名称 根目录 索引.tsv | project-remove 名称");
        std::string command = argv[1];
        if (command == "validate" && argc == 3) {
            auto p = nova::readDictionary(argv[2]);
            std::cout << "有效词条：" << p.size() << '\n';
            return 0;
        }
        if (command == "query" && argc == 3) {
            auto b = std::make_shared<nova::Backend>();
            nova::Options o;
            b->configure(o);
            nova::Session session(b);
            session.configure(o);
            session.type(argv[2]);
            for (const auto &c : session.candidates())
                std::cout << c.text << '\t' << c.reading << '\t' << c.annotation << '\n';
            return 0;
        }
        nova::Database db(nova::dataHome() / "user.db");
        if (command == "list" && argc == 2) {
            for (const auto &[name, enabled] : db.dictionaries())
                std::cout << (enabled ? "1" : "0") << '\t' << name << '\n';
        } else if (command == "projects" && argc == 2) {
            for (const auto &project : db.projects())
                std::cout << project.name << '\t' << project.root << '\t' << project.terms.size()
                          << '\n';
        } else if (command == "project-generation" && argc == 2) {
            std::cout << db.generation() << '\n';
        } else if (command == "project-import" && (argc == 5 || argc == 6)) {
            auto terms = nova::readProjectTerms(argv[4]);
            uint64_t generation = 0;
            if (argc == 6) {
                size_t used = 0;
                generation = std::stoull(argv[5], &used);
                if (!generation || used != std::string(argv[5]).size())
                    throw std::runtime_error("数据版本不合法");
            }
            db.importProject(argv[2], argv[3], terms, generation);
            std::cout << "已索引 " << terms.size() << " 个标识符\n";
        } else if (command == "project-remove" && argc == 3) {
            db.removeProject(argv[2]);
        } else if (command == "import" && argc == 4) {
            auto phrases = nova::readDictionary(argv[3]);
            db.importDictionary(argv[2], phrases);
            std::cout << "已导入 " << phrases.size() << " 条\n";
        } else if (command == "export" && argc == 3) {
            db.exportUser(argv[2]);
            std::cout << "已导出个人词条\n";
        } else if ((command == "enable" || command == "disable") && argc == 3)
            db.enableDictionary(argv[2], command == "enable");
        else if (command == "remove" && argc == 3)
            db.removeDictionary(argv[2]);
        else if (command == "clear" && argc == 3 && std::string(argv[2]) == "--yes") {
            db.clear();
            std::cout << "已清空个人学习数据和项目索引（保留安装的领域词库）\n";
        } else
            throw std::runtime_error("参数不正确；清空操作需要 clear --yes");
    } catch (const std::exception &e) {
        std::cerr << "NovaPinyin：" << e.what() << '\n';
        return 1;
    }
    return 0;
}
