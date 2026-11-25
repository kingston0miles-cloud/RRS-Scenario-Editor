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
#include    "scenario/project.h"

#include    <QApplication>
#include    <QCheckBox>
#include    <QLineEdit>
#include    <QMessageBox>
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

//------------------------------------------------------------------------------
// Записать файл с каталогами
//------------------------------------------------------------------------------
void writeFile(const QString &path, const QString &content)
{
    QDir().mkpath(QFileInfo(path).absolutePath());

    QFile f(path);
    QVERIFY2(f.open(QIODevice::WriteOnly), qPrintable(path));
    f.write(content.toUtf8());
}

//------------------------------------------------------------------------------
// Минимальный маршрут для теста закрытия: прямая T1 8 км со станциями A, B, C
//------------------------------------------------------------------------------
void buildCloseRoute(const QString &root)
{
    QString traj;

    for (int y = 0; y <= 8000; y += 500)
    {
        traj += QStringLiteral("0 %1 0 %2\n").arg(y).arg(y + 1000);
    }

    writeFile(root + "/topology/trajectories/T1.traj", traj);
    writeFile(root + "/topology/topology.xml", "<?xml version=\"1.0\"?>\n<Config/>\n");
    writeFile(root + "/topology/stations.conf",
              "Станция-A\t0\t0\t0\n"
              "Станция-B\t0\t4000\t0\n"
              "Станция-C\t0\t8000\t0\n");
}

//------------------------------------------------------------------------------
// Ответить на QMessageBox (Сохранить / Не сохранять / Отмена)
//------------------------------------------------------------------------------
void answerMessageBox(const QString &button_text)
{
    QTimer::singleShot(50, [button_text]
    {
        auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());

        if (box != nullptr)
        {
            for (QAbstractButton *b : box->buttons())
            {
                if (b->text() == button_text)
                {
                    b->click();
                    break;
                }
            }
        }
    });
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
    void closeScenarioResetsWindow();
    void undoRedoRestoresScenario();
    void workflowHintsAndStartScan();
    void mapModeHintAndKeyboard();
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

//------------------------------------------------------------------------------
// Закрытие сценария (этап 20): подтверждение несохранённых изменений и
// возврат к стартовому состоянию без перезапуска программы
//------------------------------------------------------------------------------
void TestMainWindow::closeScenarioResetsWindow()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    buildCloseRoute(dir.path() + "/routes/r");

    // Проект с одним поездом (каталог игры не требуется)
    Scenario sc;

    PlacedTrain t;
    t.name = "P1";
    t.title = "P1";
    t.trajectory = "T1";
    t.coord = 1000.0;
    t.dir = 1;
    t.length = 200.0;
    t.mass = 1.0e6;
    sc.add(t);

    ProjectInfo info;
    info.routeDir = dir.path() + "/routes/r";

    const QString project = dir.path() + "/p.rrsproj";
    QString err;
    QVERIFY2(saveProject(project, info, sc, &err), qPrintable(err));

    MainWindow w;
    QVERIFY(w.openProject(project));
    QCOMPARE(w.scenario().size(), 1);
    QVERIFY(w.route().isLoaded());
    QVERIFY(w.windowTitle() != QStringLiteral("Редактор сценариев RRS"));

    // Перенос поезда: проект изменён
    QVERIFY(w.moveTrain(0, "T1", 2000.0, 1));
    QVERIFY(w.isModified());

    // «Отмена» в подтверждении: сценарий остаётся открытым
    answerMessageBox(QStringLiteral("Отмена"));
    w.closeScenario();
    QVERIFY(w.route().isLoaded());
    QVERIFY(w.isModified());
    QCOMPARE(w.scenario().size(), 1);

    // «Не сохранять»: полный сброс к стартовому состоянию
    answerMessageBox(QStringLiteral("Не сохранять"));
    w.closeScenario();
    QVERIFY(!w.route().isLoaded());
    QCOMPARE(w.scenario().size(), 0);
    QVERIFY(!w.isModified());
    QVERIFY(w.projectPath().isEmpty());
    QCOMPARE(w.windowTitle(), QStringLiteral("Редактор сценариев RRS"));
    QCOMPARE(w.rollingStockPanel()->visibleVehicleCount(), 0);
}

//------------------------------------------------------------------------------
// Отмена и повтор (этап 24): переносы и удаления возвращаются Ctrl+Z/Ctrl+Y
//------------------------------------------------------------------------------
void TestMainWindow::undoRedoRestoresScenario()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    buildCloseRoute(dir.path() + "/routes/r");

    Scenario sc;

    PlacedTrain t;
    t.name = "P1";
    t.title = "P1";
    t.trajectory = "T1";
    t.coord = 1000.0;
    t.dir = 1;
    t.length = 200.0;
    t.mass = 1.0e6;
    sc.add(t);

    ProjectInfo info;
    info.routeDir = dir.path() + "/routes/r";

    const QString project = dir.path() + "/p.rrsproj";
    QString err;
    QVERIFY2(saveProject(project, info, sc, &err), qPrintable(err));

    MainWindow w;
    QVERIFY(w.openProject(project));
    QVERIFY(!w.canUndo());
    QVERIFY(!w.canRedo());

    // Перенос и удаление - два действия в истории
    QVERIFY(w.moveTrain(0, "T1", 2000.0, 1));
    QCOMPARE(w.scenario().trains()[0].coord, 2000.0);
    QVERIFY(w.canUndo());

    w.removeTrain(0);
    QCOMPARE(w.scenario().size(), 0);

    // Отмена: возврат поезда на место переноса, затем на исходное
    w.undo();
    QCOMPARE(w.scenario().size(), 1);
    QCOMPARE(w.scenario().trains()[0].coord, 2000.0);

    w.undo();
    QCOMPARE(w.scenario().size(), 1);
    QCOMPARE(w.scenario().trains()[0].coord, 1000.0);
    QVERIFY(w.canRedo());

    // Повтор: перенос и удаление снова
    w.redo();
    QCOMPARE(w.scenario().trains()[0].coord, 2000.0);

    w.redo();
    QCOMPARE(w.scenario().size(), 0);
    QVERIFY(!w.canRedo());
    QVERIFY(w.canUndo());
}

//------------------------------------------------------------------------------
// Панель шагов, справка по клавишам и сканирование маршрутов (этапы 22-23)
//------------------------------------------------------------------------------
void TestMainWindow::workflowHintsAndStartScan()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    // Игровой корень: один настоящий маршрут и одна пустая папка
    buildCloseRoute(dir.path() + "/routes/r");
    QDir(dir.path() + "/routes/junk").mkpath(QStringLiteral("."));

    const QStringList routes = MainWindow::scanRoutes(dir.path());
    QCOMPARE(routes.size(), 1);
    QCOMPARE(routes.first(), QStringLiteral("r"));

    // Проект с одним поездом
    Scenario sc;

    PlacedTrain t;
    t.name = "P1";
    t.title = "P1";
    t.trajectory = "T1";
    t.coord = 1000.0;
    t.dir = 1;
    t.length = 200.0;
    t.mass = 1.0e6;
    sc.add(t);

    ProjectInfo info;
    info.routeDir = dir.path() + "/routes/r";

    const QString project = dir.path() + "/p.rrsproj";
    QString err;
    QVERIFY2(saveProject(project, info, sc, &err), qPrintable(err));

    MainWindow w;
    QVERIFY(w.openProject(project));

    // Шаг «маршрут» выполнен, текущий - «составы»
    QVERIFY(w.workflowBar()->state().done(1));
    QCOMPARE(w.workflowBar()->currentStep(), 2);

    // Справка по клавишам: проверка F7 и экспорт Ctrl+E присутствуют
    const QString hints = w.shortcutHints().join(QStringLiteral(" | "));
    QVERIFY2(hints.contains(QStringLiteral("F7")), qPrintable(hints));
    QVERIFY2(hints.contains(QStringLiteral("Ctrl+E")), qPrintable(hints));
    QVERIFY2(hints.contains(QStringLiteral("Esc")), qPrintable(hints));

    // Разбивка замечаний загрузки
    QVERIFY(!w.warningsBreakdown().isEmpty());
}

//------------------------------------------------------------------------------
// Полоса режима карты и клавиатура (аудит 1.4, 5.3)
//------------------------------------------------------------------------------
void TestMainWindow::mapModeHintAndKeyboard()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    buildCloseRoute(dir.path() + "/routes/r");

    MainWindow w;
    QVERIFY(w.openRoute(dir.path() + "/routes/r"));

    // Обычный режим - подсказки нет
    QVERIFY(w.map()->mapModeHint().isEmpty());

    // Режим выбора точки
    w.map()->setPickMode(true);
    QVERIFY(w.map()->mapModeHint().contains(QStringLiteral("Выбор точки")));
    w.map()->setPickMode(false);

    // Режим расстановки
    PlacedTrain ghost;
    ghost.title = QStringLiteral("Состав-призрак");
    w.map()->setPlacementTemplate(&ghost);
    QVERIFY(w.map()->mapModeHint().contains(QStringLiteral("Расстановка")));
    QVERIFY(w.map()->mapModeHint().contains(QStringLiteral("Esc")));
    w.map()->setPlacementTemplate(nullptr);

    // Клавиатура карты: масштаб, перемещение, поворот - без сбоев
    QTest::keyClick(w.map(), Qt::Key_Plus);
    QTest::keyClick(w.map(), Qt::Key_Minus);
    QTest::keyClick(w.map(), Qt::Key_Left);
    QTest::keyClick(w.map(), Qt::Key_Right);
    QTest::keyClick(w.map(), Qt::Key_Up);
    QTest::keyClick(w.map(), Qt::Key_Down);
    QTest::keyClick(w.map(), Qt::Key_R);
    QTest::keyClick(w.map(), Qt::Key_F);
}

QTEST_MAIN(TestMainWindow)

#include "test-main-window.moc"
