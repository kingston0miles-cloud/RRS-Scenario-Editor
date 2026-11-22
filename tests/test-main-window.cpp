#include    <QtTest>

#include    <QDir>
#include    <QFile>
#include    <QImage>
#include    <QSettings>
#include    <QStandardPaths>
#include    <QTemporaryDir>

#include    "ui/main-window.h"
#include    "ui/consist-editor-dialog.h"
#include    "ui/map-widget.h"
#include    "ui/place-train-dialog.h"
#include    "ui/rolling-stock-panel.h"
#include    "ui/trains-panel.h"

#include    <QApplication>
#include    <QCheckBox>
#include    <QLineEdit>
#include    <QPushButton>
#include    <QTimer>

using namespace rse;

namespace
{

//------------------------------------------------------------------------------
// Сохранить картинку, если задан каталог RSE_SHOT_DIR (для просмотра глазами)
//------------------------------------------------------------------------------
void saveShot(const QImage &img, const QString &name)
{
    const QString dir = qEnvironmentVariable("RSE_SHOT_DIR");

    if (!dir.isEmpty())
    {
        img.save(dir + "/" + name);
    }
}

} // namespace

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
class TestMainWindow : public QObject
{
    Q_OBJECT

private:

    QTemporaryDir settings_dir_;

private slots:

    void initTestCase();
    void init();

    void routeOutsideGameHasNoCatalog();
    void gameIsFoundByRoute();
    void lastGameIsUsedForRouteOutsideGame();
    void badGameDirectoryKeepsPreviousCatalog();
    void placeTrainByClick();
    void cancelledDialogPlacesNothing();
    void overlapBlocksPlacement();
    void reverseAndRemoveTrain();
    void splitTrainIntoParts();
};

//------------------------------------------------------------------------------
// Настройки и данные программы - во временный каталог, а не в профиль пользователя
//------------------------------------------------------------------------------
void TestMainWindow::initTestCase()
{
    QVERIFY(settings_dir_.isValid());

    QStandardPaths::setTestModeEnabled(true);

    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings_dir_.path());
}

//------------------------------------------------------------------------------
// Каждый тест начинает с чистых настроек
//------------------------------------------------------------------------------
void TestMainWindow::init()
{
    QSettings settings("rrs-scenario-editor", "rrs-scenario-editor");
    settings.clear();
    settings.sync();
}

//------------------------------------------------------------------------------
// Маршрут, скопированный (ссылкой) отдельно от игры: рядом нет cfg/vehicles
//------------------------------------------------------------------------------
void TestMainWindow::routeOutsideGameHasNoCatalog()
{
    const QString route = qEnvironmentVariable("RRS_ROUTE_DIR");

    if (route.isEmpty())
    {
        QSKIP("RRS_ROUTE_DIR не задан");
    }

    QTemporaryDir lonely;
    QVERIFY(lonely.isValid());
    QVERIFY(QDir().mkpath(lonely.path() + "/routes"));

    if (!QFile::link(route, lonely.path() + "/routes/r"))
    {
        QSKIP("Не удалось создать символическую ссылку на маршрут");
    }

    MainWindow w;
    QVERIFY(w.openRoute(lonely.path() + "/routes/r"));

    // Маршрут открылся, подвижного состава нет, и это не ошибка
    QVERIFY(!w.catalog().isLoaded());
    QCOMPARE(w.rollingStockPanel()->visibleVehicleCount(), 0);
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void TestMainWindow::gameIsFoundByRoute()
{
    const QString route = qEnvironmentVariable("RRS_ROUTE_DIR");

    if (route.isEmpty())
    {
        QSKIP("RRS_ROUTE_DIR не задан");
    }

    MainWindow w;
    w.resize(1400, 760);
    w.show();

    QVERIFY(w.openRoute(route));

    QVERIFY(w.catalog().isLoaded());
    QCOMPARE(w.catalog().vehicles().size(), 10);
    QCOMPARE(w.catalog().trains().size(), 10);
    QCOMPARE(w.rollingStockPanel()->visibleVehicleCount(), 10);
    QCOMPARE(w.rollingStockPanel()->visibleTrainCount(), 10);

    // Дать интерфейсу перекомпоноваться после смены текстов, как это сделает
    // цикл событий в живой программе
    QTest::qWait(50);

    // Центральный виджет и панель реально отрисованы
    const QImage shot = w.grab().toImage();
    saveShot(shot, "window.png");
    QVERIFY(shot.width() >= 1000);
}

//------------------------------------------------------------------------------
// Игра, указанная при прошлом запуске, подхватывается для маршрута вне игры
//------------------------------------------------------------------------------
void TestMainWindow::lastGameIsUsedForRouteOutsideGame()
{
    const QString route = qEnvironmentVariable("RRS_ROUTE_DIR");

    if (route.isEmpty())
    {
        QSKIP("RRS_ROUTE_DIR не задан");
    }

    QTemporaryDir lonely;
    QVERIFY(QDir().mkpath(lonely.path() + "/routes"));

    if (!QFile::link(route, lonely.path() + "/routes/r"))
    {
        QSKIP("Не удалось создать символическую ссылку на маршрут");
    }

    // Первый запуск: маршрут внутри игры запоминает каталог игры
    {
        MainWindow first;
        QVERIFY(first.openRoute(route));
        QVERIFY(first.catalog().isLoaded());
    }

    // Второй запуск: маршрут вне игры, каталог берётся из настроек
    MainWindow second;
    QVERIFY(second.openRoute(lonely.path() + "/routes/r"));
    QVERIFY(second.catalog().isLoaded());
    QCOMPARE(second.catalog().vehicles().size(), 10);
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void TestMainWindow::badGameDirectoryKeepsPreviousCatalog()
{
    const QString route = qEnvironmentVariable("RRS_ROUTE_DIR");

    if (route.isEmpty())
    {
        QSKIP("RRS_ROUTE_DIR не задан");
    }

    MainWindow w;
    QVERIFY(w.openRoute(route));
    QVERIFY(w.catalog().isLoaded());

    QTemporaryDir empty;

    // Ошибка без диалога (show_errors = false), чтобы тест не завис на окне
    QVERIFY(!w.openGame(empty.path(), false));

    // Прежний каталог при ошибочном выборе остаётся нетронутым
    QVERIFY(w.catalog().isLoaded());
    QCOMPARE(w.catalog().vehicles().size(), 10);
    QCOMPARE(w.rollingStockPanel()->visibleVehicleCount(), 10);
    QVERIFY(w.catalog().gameRoot() == QDir(VehicleCatalog::gameRootForRoute(route)).absolutePath());
}

namespace
{

//------------------------------------------------------------------------------
// Через 50 мс найти открытый модальный диалог расстановки и выполнить действие
//------------------------------------------------------------------------------
template <typename F>
void withDialog(F action)
{
    QTimer::singleShot(50, [action]
    {
        auto *dlg = qobject_cast<PlaceTrainDialog *>(QApplication::activeModalWidget());

        if (dlg != nullptr)
        {
            action(dlg);
        }
    });
}

Route::Hit hitOn(const Route &route, const QString &name, double coord)
{
    Route::Hit h;
    const TrajectoryData *t = route.find(name);

    if (t != nullptr)
    {
        h.valid = true;
        h.trajectory = name;
        h.coord = coord;
        h.pos = t->pointAt(coord);
    }

    return h;
}

} // namespace

//------------------------------------------------------------------------------
// Выбрали состав в панели, кликнули по пути - диалог, «Развернуть», «Поставить»
//------------------------------------------------------------------------------
void TestMainWindow::placeTrainByClick()
{
    const QString route = qEnvironmentVariable("RRS_ROUTE_DIR");

    if (route.isEmpty())
    {
        QSKIP("RRS_ROUTE_DIR не задан");
    }

    MainWindow w;
    w.resize(1400, 760);
    w.show();
    QVERIFY(w.openRoute(route));

    // Пока ничего не выбрано, щелчок по пути ничего не делает
    w.placeAt(hitOn(w.route(), "track_a_p1a", 100.0));
    QCOMPARE(w.scenario().size(), 0);
    QVERIFY(!w.map()->placementActive());

    const QString train_name = w.catalog().trains().first().name;
    QVERIFY(w.rollingStockPanel()->selectTrain(train_name));
    QVERIFY(w.map()->placementActive());

    bool flipped_text_changed = false;

    withDialog([&](PlaceTrainDialog *dlg)
    {
        // Голова по умолчанию по направлению пути
        const QString dir0 = PlaceTrainDialog::directionText(w.route(), "track_a_p1a", 1);
        flipped_text_changed = (dir0 != PlaceTrainDialog::directionText(w.route(), "track_a_p1a", -1));

        QVERIFY(dlg->result().dir == 1);
        dlg->flipButton()->click();
        QVERIFY(dlg->result().dir == -1);
        QVERIFY(dlg->okButton()->isEnabled() || !dlg->warningsText().isEmpty());

        dlg->nameEdit()->setText("Первый");
        dlg->okButton()->click();
    });

    const double len = w.route().find("track_a_p1a")->length;
    w.placeAt(hitOn(w.route(), "track_a_p1a", len / 2.0));

    QVERIFY(flipped_text_changed);
    QCOMPARE(w.scenario().size(), 1);

    const PlacedTrain &t = w.scenario().trains().first();
    QCOMPARE(t.name, QString("Первый"));
    QCOMPARE(t.trajectory, QString("track_a_p1a"));
    QCOMPARE(t.dir, -1);
    QVERIFY(t.length > 0.0);
    QCOMPARE(w.trainsPanel()->rowCount(), 1);

    QTest::qWait(30);
    saveShot(w.grab().toImage(), "placed.png");
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void TestMainWindow::cancelledDialogPlacesNothing()
{
    const QString route = qEnvironmentVariable("RRS_ROUTE_DIR");

    if (route.isEmpty())
    {
        QSKIP("RRS_ROUTE_DIR не задан");
    }

    MainWindow w;
    QVERIFY(w.openRoute(route));
    QVERIFY(w.rollingStockPanel()->selectTrain(w.catalog().trains().first().name));

    withDialog([](PlaceTrainDialog *dlg) { dlg->reject(); });
    w.placeAt(hitOn(w.route(), "track_a_p1a", 100.0));

    QCOMPARE(w.scenario().size(), 0);
}

//------------------------------------------------------------------------------
// Второй состав на то же место: кнопка «Поставить» заблокирована
//------------------------------------------------------------------------------
void TestMainWindow::overlapBlocksPlacement()
{
    const QString route = qEnvironmentVariable("RRS_ROUTE_DIR");

    if (route.isEmpty())
    {
        QSKIP("RRS_ROUTE_DIR не задан");
    }

    MainWindow w;
    QVERIFY(w.openRoute(route));
    QVERIFY(w.rollingStockPanel()->selectTrain(w.catalog().trains().first().name));

    const double len = w.route().find("track_a_p1a")->length;

    withDialog([](PlaceTrainDialog *dlg) { dlg->okButton()->click(); });
    w.placeAt(hitOn(w.route(), "track_a_p1a", len / 2.0));
    QCOMPARE(w.scenario().size(), 1);

    bool blocked = false;

    withDialog([&](PlaceTrainDialog *dlg)
    {
        blocked = !dlg->okButton()->isEnabled() && dlg->warningsText().contains("пересекается");
        dlg->reject();
    });
    w.placeAt(hitOn(w.route(), "track_a_p1a", len / 2.0));

    QVERIFY(blocked);
    QCOMPARE(w.scenario().size(), 1);
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void TestMainWindow::reverseAndRemoveTrain()
{
    const QString route = qEnvironmentVariable("RRS_ROUTE_DIR");

    if (route.isEmpty())
    {
        QSKIP("RRS_ROUTE_DIR не задан");
    }

    MainWindow w;
    QVERIFY(w.openRoute(route));
    QVERIFY(w.rollingStockPanel()->selectTrain(w.catalog().trains().first().name));

    const double len = w.route().find("track_a_p1a")->length;

    withDialog([](PlaceTrainDialog *dlg) { dlg->okButton()->click(); });
    w.placeAt(hitOn(w.route(), "track_a_p1a", len / 2.0));
    QCOMPARE(w.scenario().size(), 1);

    const PlacedTrain before = w.scenario().trains().first();

    w.reverseTrain(0);

    const PlacedTrain after = w.scenario().trains().first();
    QVERIFY(after.dir != before.dir || after.trajectory != before.trajectory);
    QCOMPARE(after.groups.size(), before.groups.size());

    w.reverseTrain(0);
    QCOMPARE(w.scenario().trains().first().trajectory, before.trajectory);
    QVERIFY(qAbs(w.scenario().trains().first().coord - before.coord) < 1e-6);

    w.removeTrain(0);
    QCOMPARE(w.scenario().size(), 0);
    QCOMPARE(w.trainsPanel()->rowCount(), 0);
}

//------------------------------------------------------------------------------
// Правка состава: добавить вагоны к локомотиву и разделить на два поезда
//------------------------------------------------------------------------------
void TestMainWindow::splitTrainIntoParts()
{
    const QString route = qEnvironmentVariable("RRS_ROUTE_DIR");

    if (route.isEmpty())
    {
        QSKIP("RRS_ROUTE_DIR не задан");
    }

    MainWindow w;
    w.resize(1400, 760);
    w.show();
    QVERIFY(w.openRoute(route));
    QVERIFY(w.rollingStockPanel()->selectVehicle("vl60k", "vl60k-1737"));

    withDialog([](PlaceTrainDialog *dlg) { dlg->okButton()->click(); });
    w.placeAt(hitOn(w.route(), "track_a_p1a", w.route().find("track_a_p1a")->length * 0.8));
    QCOMPARE(w.scenario().size(), 1);

    // Окно редактора: к локомотиву 6 вагонов, разрыв перед третьей единицей
    ConsistEditorDialog dlg(w.scenario(), w.route(), w.catalog(), w.scenario().trains()[0], 0);
    QVERIFY(dlg.addVehicle("IMR_pass_rzd", "IMR_pass_rzd-11100", 6, 0.5));
    dlg.setCutBefore(3, true);
    dlg.show();
    QTest::qWait(30);
    saveShot(dlg.grab().toImage(), "consist-editor.png");

    QVERIFY2(dlg.okButton()->isEnabled(), qPrintable(dlg.messagesText()));

    QVERIFY(w.applyConsistEdit(0, dlg.edited(), dlg.cuts()));

    QCOMPARE(w.scenario().size(), 2);
    QCOMPARE(w.trainsPanel()->rowCount(), 2);

    // Первая часть: локомотив + 2 вагона, вторая: 4 вагона
    int units0 = 0;
    int units1 = 0;

    for (const ConsistGroup &g : w.scenario().trains()[0].groups) units0 += g.count;
    for (const ConsistGroup &g : w.scenario().trains()[1].groups) units1 += g.count;

    QCOMPARE(units0, 3);
    QCOMPARE(units1, 4);

    // Части не пересекаются и не сцеплены
    const PlacedTrain &a = w.scenario().trains()[0];
    const PlacedTrain &b = w.scenario().trains()[1];
    const Footprint fa = computeFootprint(w.route(), a.trajectory, a.coord, a.dir, a.length);
    const Footprint fb = computeFootprint(w.route(), b.trajectory, b.coord, b.dir, b.length);
    QVERIFY(overlapLength(fa, fb) == 0.0);

    QTest::qWait(30);
    saveShot(w.grab().toImage(), "split.png");
}

QTEST_MAIN(TestMainWindow)

#include "test-main-window.moc"
