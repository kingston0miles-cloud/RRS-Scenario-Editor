#include    <QtTest>

#include    <QFile>
#include    <QTranslator>

//------------------------------------------------------------------------------
// Английский перевод (этап 30): .qm встроен в ресурс rse-ui, строки
// попадают в контексты классов
//------------------------------------------------------------------------------
class TestTranslation : public QObject
{
    Q_OBJECT

private slots:

    void loadsAndTranslates();
};

void TestTranslation::loadsAndTranslates()
{
    if (!QFile::exists(QStringLiteral(":/translations/rse-editor_en.qm")))
    {
        QSKIP("перевод не встроен: LinguistTools не найден");
    }

    QTranslator t;
    QVERIFY2(t.load(QStringLiteral(":/translations/rse-editor_en")),
             "не удалось загрузить rse-editor_en.qm из ресурса");

    // Контекст класса окна графика
    QCOMPARE(t.translate("rse::TimetableDialog", "Прибытие"), QStringLiteral("Arrival"));
    QCOMPARE(t.translate("rse::TimetableDialog", "Платформы"), QStringLiteral("Platforms"));

    // Контекст главного окна с мнемоникой
    QCOMPARE(t.translate("rse::MainWindow", "&Файл"), QStringLiteral("&File"));

    // Контекст свободных функций
    QCOMPARE(t.translate("QObject", "«%1»: %2"), QStringLiteral("\"%1\": %2"));
    QCOMPARE(t.translate("QObject", "Отправление"), QStringLiteral("Departure"));

    // Отсутствующая строка - пусто
    QVERIFY(t.translate("rse::TimetableDialog", "нет такой строки").isEmpty());
}

QTEST_MAIN(TestTranslation)

#include    "test-translation.moc"
