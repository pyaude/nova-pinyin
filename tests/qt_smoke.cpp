// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>
#include <QLineEdit>
#include <fstream>
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    if (argc != 2)
        return 2;
    QLineEdit entry;
    entry.setWindowTitle("NovaPinyin Qt Smoke");
    entry.resize(500, 100);
    QObject::connect(&entry, &QLineEdit::textChanged, [&](const QString &s) {
        std::ofstream out(argv[1]);
        out << s.toUtf8().constData();
    });
    entry.show();
    entry.setFocus();
    return app.exec();
}
