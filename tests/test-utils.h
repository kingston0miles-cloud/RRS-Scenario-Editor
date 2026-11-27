//------------------------------------------------------------------------------
// Общие помощники тестов: запись и чтение файлов фикстур
//------------------------------------------------------------------------------
#pragma once

#include    <QDir>
#include    <QFile>
#include    <QFileInfo>
#include    <QtTest>

//------------------------------------------------------------------------------
// Записать текстовый файл (UTF-8), создав каталоги при необходимости
//------------------------------------------------------------------------------
inline void writeFile(const QString &path, const QString &content)
{
    QDir().mkpath(QFileInfo(path).absolutePath());

    QFile f(path);
    QVERIFY2(f.open(QIODevice::WriteOnly), qPrintable(path));
    f.write(content.toUtf8());
}

//------------------------------------------------------------------------------
// Прочитать текстовый файл; пустая строка - файл не открылся
//------------------------------------------------------------------------------
inline QString readFile(const QString &path)
{
    QFile f(path);

    if (!f.open(QIODevice::ReadOnly))
    {
        return QString();
    }

    return QString::fromUtf8(f.readAll());
}
