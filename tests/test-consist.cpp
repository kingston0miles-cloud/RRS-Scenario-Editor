#include    <QtTest>

#include    <QDir>
#include    <QFile>
#include    <QTemporaryDir>

#include    "scenario/consist.h"
#include    "scenario/export.h"
#include    "ui/consist-editor-dialog.h"
#include    "test-utils.h"

#include    <QApplication>
#include    <QLineEdit>
#include    <QTimer>

#include    "ui/main-window.h"
#include    "ui/check-dialog.h"
#include    "scenario/checker.h"
#include    "ui/continuation-dialog.h"
#include    "ui/place-train-dialog.h"
#include    "ui/rolling-stock-panel.h"
#include    "ui/trains-panel.h"
#include    "ui/event-dialog.h"
#include    "ui/events-panel.h"
#include    <QPushButton>
#include    <QMouseEvent>
#include    <QDoubleSpinBox>
#include    <QComboBox>
#include    <QLayout>
#include    "ui/map-widget.h"

using namespace rse;

namespace
{

QString vehicleXml(double length, double empty, double payload, bool crane, bool autopilot)
{
    QString s = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<Config>\n<Vehicle>\n";
    s += QString("<EmptyMass>%1</EmptyMass>\n<PayloadMass>%2</PayloadMass>\n<Length>%3</Length>\n")
             .arg(empty).arg(payload).arg(length);

    if (crane)
    {
        s += "<BrakeCraneModule>krm394</BrakeCraneModule>\n";
    }

    s += autopilot ? "<AutopilotModule>ap</AutopilotModule>\n" : "<AutopilotModule></AutopilotModule>\n";
    s += "</Vehicle>\n</Config>\n";

    return s;
}

QString groupXml(const QString &mdir, const QString &mod, const QString &cdir,
                 const QString &cfg, int count)
{
    return QString("<Vehicle>\n<ModuleDir>%1</ModuleDir>\n<Module>%2</Module>\n"
                   "<ModuleConfigDir>%3</ModuleConfigDir>\n<ModuleConfig>%4</ModuleConfig>\n"
                   "<IsOrientationForward>1</IsOrientationForward>\n<Count>%5</Count>\n"
                   "<PayloadCoeff>0.0</PayloadCoeff>\n</Vehicle>\n")
        .arg(mdir, mod, cdir, cfg).arg(count);
}

//------------------------------------------------------------------------------
// Игра: локомотив L (20 м, автопилот), вагон W (10 м, 10 т + 50 т груза),
// готовый состав «поезд» = L + 3 W
//------------------------------------------------------------------------------
void buildGame(const QString &root)
{
    writeFile(root + "/cfg/vehicles/locos/L.xml", vehicleXml(20.0, 100000.0, 0.0, true, true));
    writeFile(root + "/cfg/vehicles/cars/W.xml", vehicleXml(10.0, 10000.0, 50000.0, false, false));

    writeFile(root + "/cfg/trains/poezd.xml",
              "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<Config>\n<Common>\n"
              "<ChargingPressure>0.5</ChargingPressure>\n<Title>t</Title>\n</Common>\n"
              + groupXml("loco", "l", "locos", "L", 1) + groupXml("car", "w", "cars", "W", 3)
              + "</Config>\n");
}

//------------------------------------------------------------------------------
// Прямой путь T длиной 400 м и тупик короткий S длиной 25 м
//------------------------------------------------------------------------------
void buildRoute(const QString &root)
{
    writeFile(root + "/topology/trajectories/T.traj", "0 0 0 0\n0 400 0 400\n");
    writeFile(root + "/topology/trajectories/S.traj", "50 0 0 0\n50 25 0 25\n");
    writeFile(root + "/topology/topology.xml", "<Config></Config>\n");
}

PlacedTrain trainFrom(const VehicleCatalog &cat, const QString &train_name,
                      const QString &traj, double coord, int dir)
{
    PlacedTrain t;
    const TrainConfigData *cfg = cat.findTrain(train_name);
    t.name = "P";
    t.title = train_name;
    t.groups = cfg->groups;
    t.trajectory = traj;
    t.coord = coord;
    t.dir = dir;
    updateSummary(t, cat);

    return t;
}

} // namespace

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
class TestConsist : public QObject
{
    Q_OBJECT

private:

    QTemporaryDir game_dir_;
    QTemporaryDir route_dir_;
    VehicleCatalog cat_;
    Route route_;

private slots:

    void initTestCase();

    void flattenAndCompress();
    void summaryFollowsUnits();
    void noSplitKeepsOnePiece();
    void splitPlacesPartsBehindWithGap();
    void splitHeadingTowardsStart();
    void splitDoesNotFitOnTrack();
    void splitConflictsWithOtherTrain();
    void autoModeOnlyForCapableHead();
    void dialogEditsUnits();
    void dialogBlocksConflicts();
    void saveTrainRoundTrip();
    void mainWindowExportsAndReopensProject();
    void mainWindowEventsWithMapPick();
    void mainWindowImportsScenario();
    void dragAndRelocateTrain();
    void continuationFlow();
    void saveTrainRejectsBadInput();
    void composerDialog();
    void mainWindowComposesAndSavesTrain();
};

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void TestConsist::initTestCase()
{
    QVERIFY(game_dir_.isValid() && route_dir_.isValid());
    buildGame(game_dir_.path());
    buildRoute(route_dir_.path());

    QVERIFY2(cat_.load(game_dir_.path()), qPrintable(cat_.errorString()));
    QVERIFY2(route_.load(route_dir_.path()), qPrintable(route_.errorString()));
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void TestConsist::flattenAndCompress()
{
    const PlacedTrain t = trainFrom(cat_, "poezd", "T", 200.0, 1);

    const QVector<ConsistGroup> flat = flattenGroups(t.groups);
    QCOMPARE(flat.size(), 4);

    for (const ConsistGroup &u : flat)
    {
        QCOMPARE(u.count, 1);
    }

    const QVector<ConsistGroup> back = compressUnits(flat);
    QCOMPARE(back.size(), 2);
    QCOMPARE(back[1].count, 3);

    // Разная загрузка - разные группы
    QVector<ConsistGroup> mixed = flat;
    mixed[2].payloadCoeff = 0.5;
    QCOMPARE(compressUnits(mixed).size(), 4);
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void TestConsist::summaryFollowsUnits()
{
    PlacedTrain t = trainFrom(cat_, "poezd", "T", 200.0, 1);

    QVERIFY(qAbs(t.length - 50.0) < 1e-6);
    QVERIFY(t.autopilotCapable);

    // Загрузить вагоны полностью: 100 + 3*(10+50) т
    for (ConsistGroup &g : t.groups)
    {
        if (g.config == "W")
        {
            g.payloadCoeff = 1.0;
        }
    }

    updateSummary(t, cat_);
    QVERIFY(qAbs(t.mass - 280000.0) < 1e-6);
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void TestConsist::noSplitKeepsOnePiece()
{
    const Scenario sc;
    const PlacedTrain t = trainFrom(cat_, "poezd", "T", 200.0, 1);

    const EditPlan plan = planEdit(sc, route_, cat_, -1, t, {});

    QVERIFY(plan.ok());
    QCOMPARE(plan.pieces.size(), 1);
    QCOMPARE(plan.pieces[0].name, QString("P"));
    QCOMPARE(plan.pieces[0].trajectory, QString("T"));
}

//------------------------------------------------------------------------------
// L W W W, разрыв перед 2-й и 4-й единицами: L+W (30 м) | W (10 м) | W (10 м)
// Голова на 200 м, хвост идёт к началу
//------------------------------------------------------------------------------
void TestConsist::splitPlacesPartsBehindWithGap()
{
    const Scenario sc;
    const PlacedTrain t = trainFrom(cat_, "poezd", "T", 200.0, 1);

    const EditPlan plan = planEdit(sc, route_, cat_, -1, t, { 2, 3 });

    QVERIFY2(plan.ok(), qPrintable(plan.errors.join("; ")));
    QCOMPARE(plan.pieces.size(), 3);

    // Часть 1: L+W, голова 200
    QVERIFY(qAbs(plan.pieces[0].length - 30.0) < 1e-6);
    QVERIFY(qAbs(plan.pieces[0].coord - 200.0) < 1e-6);

    // Часть 2: голова на 200 - (30 + 15) = 155, дальше 10 м вагон
    QCOMPARE(plan.pieces[1].name, QString("P (2)"));
    QVERIFY(qAbs(plan.pieces[1].coord - 155.0) < 1e-6);
    QCOMPARE(plan.pieces[1].dir, 1);
    QVERIFY(qAbs(plan.pieces[1].length - 10.0) < 1e-6);
    QVERIFY(!plan.pieces[1].autoMode);

    // Часть 3: 155 - (10 + 15) = 130
    QVERIFY(qAbs(plan.pieces[2].coord - 130.0) < 1e-6);

    // Между частями зазор SPLIT_GAP, пересечений нет
    Scenario work;
    for (const PlacedTrain &p : plan.pieces)
    {
        const PlacementCheck chk = work.check(route_, p);
        QVERIFY(chk.overlap == 0.0);
        QVERIFY(chk.nearTrains.isEmpty());
        work.add(p);
    }
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void TestConsist::splitHeadingTowardsStart()
{
    const Scenario sc;
    const PlacedTrain t = trainFrom(cat_, "poezd", "T", 100.0, -1);

    const EditPlan plan = planEdit(sc, route_, cat_, -1, t, { 1 });

    QVERIFY2(plan.ok(), qPrintable(plan.errors.join("; ")));
    QCOMPARE(plan.pieces.size(), 2);

    // Хвост идёт к концу траектории: вторая часть голова на 100 + 20 + 15
    QVERIFY(qAbs(plan.pieces[1].coord - 135.0) < 1e-6);
    QCOMPARE(plan.pieces[1].dir, -1);
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void TestConsist::splitDoesNotFitOnTrack()
{
    const Scenario sc;

    // На тупике 25 м: голова на 25 м, L+W (30 м) уже не умещается
    const PlacedTrain t = trainFrom(cat_, "poezd", "S", 25.0, -1);

    const EditPlan plan = planEdit(sc, route_, cat_, -1, t, {});
    QVERIFY(!plan.ok());
    QVERIFY(!plan.errors.isEmpty());

    // Часть, уходящая за пределы пути, тоже ошибка
    const PlacedTrain t2 = trainFrom(cat_, "poezd", "T", 60.0, 1);
    const EditPlan plan2 = planEdit(sc, route_, cat_, -1, t2, { 2 });
    QVERIFY(!plan2.ok());
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void TestConsist::splitConflictsWithOtherTrain()
{
    Scenario sc;

    PlacedTrain blocker = trainFrom(cat_, "poezd", "T", 140.0, 1);
    blocker.name = "Другой";
    sc.add(blocker);   // занимает [90, 140]

    // Вторая часть попадёт на 200 - 45 = 155 .. 145 - зона сцепки с «Другой», а третья -
    // на 130 .. 120, пересекается
    const PlacedTrain t = trainFrom(cat_, "poezd", "T", 200.0, 1);
    const EditPlan plan = planEdit(sc, route_, cat_, -1, t, { 2, 3 });

    QVERIFY(!plan.ok());
    QVERIFY(plan.errors.join(" ").contains("Другой"));
}

//------------------------------------------------------------------------------
// Флаг бота остаётся только у головной части, и только если у неё есть автопилот
//------------------------------------------------------------------------------
void TestConsist::autoModeOnlyForCapableHead()
{
    const Scenario sc;
    PlacedTrain t = trainFrom(cat_, "poezd", "T", 200.0, 1);
    t.autoMode = true;

    EditPlan plan = planEdit(sc, route_, cat_, -1, t, { 1 });
    QVERIFY(plan.ok());

    // Часть 1 - один локомотив (автопилот есть), часть 2 - вагоны
    QVERIFY(plan.pieces[0].autoMode);
    QVERIFY(!plan.pieces[1].autoMode);

    // Без локомотива в голове и хвосте автоведение невозможно
    PlacedTrain w = t;
    w.groups.removeFirst();
    plan = planEdit(sc, route_, cat_, -1, w, {});
    QVERIFY(plan.ok());
    QVERIFY(!plan.pieces[0].autoMode);
    QVERIFY(!plan.pieces[0].autopilotCapable);
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void TestConsist::dialogEditsUnits()
{
    Scenario sc;
    sc.add(trainFrom(cat_, "poezd", "T", 200.0, 1));

    ConsistEditorDialog dlg(sc, route_, cat_, sc.trains()[0], 0);

    QCOMPARE(dlg.unitCount(), 4);

    // Любое число вагонов к локомотиву: +5 вагонов в конец, 50 % груза
    QVERIFY(dlg.addVehicle("cars", "W", 5, 0.5));
    QCOMPARE(dlg.unitCount(), 9);
    QVERIFY(qAbs(dlg.edited().length - 100.0) < 1e-6);

    // Вторая локомотивная единица в хвост (вторая тяга) и готовый состав целиком
    QVERIFY(dlg.addVehicle("locos", "L", 1, 0.0, 9));
    QVERIFY(dlg.addTrain("poezd", 1));
    QCOMPARE(dlg.unitCount(), 14);

    dlg.removeUnits({ 9, 10, 11, 12, 13 });
    QCOMPARE(dlg.unitCount(), 9);

    // Перестановка: локомотив вниз на 1
    dlg.moveUnit(0, 1);
    QCOMPARE(dlg.edited().groups.first().config, QString("W"));

    // Загрузка и разворот
    dlg.setPayload({ 0 }, 1.0);
    dlg.flipUnits({ 0 });

    const PlacedTrain e = dlg.edited();
    QCOMPARE(e.groups.first().config, QString("W"));
    QVERIFY(e.groups.first().payloadCoeff == 1.0);
    QVERIFY(!e.groups.first().forward);

    // Разрыв перед 4-й единицей
    dlg.setCutBefore(3, true);
    QCOMPARE(dlg.cuts(), QVector<int>() << 3);
    QVERIFY(dlg.plan().ok());
    QCOMPARE(dlg.plan().pieces.size(), 2);

    // Перед первой единицей разрыв невозможен
    dlg.setCutBefore(0, true);
    QCOMPARE(dlg.cuts(), QVector<int>() << 3);

    QVERIFY(!dlg.addVehicle("cars", "нет-такого", 1, 1.0));
}

//------------------------------------------------------------------------------
// Состав вырос и пересёкся с соседом - «Применить» заблокировано
//------------------------------------------------------------------------------
void TestConsist::dialogBlocksConflicts()
{
    Scenario sc;
    sc.add(trainFrom(cat_, "poezd", "T", 200.0, 1));   // [150, 200]

    PlacedTrain other = trainFrom(cat_, "poezd", "T", 90.0, 1);
    other.name = "Сосед";
    sc.add(other);   // [40, 90]

    ConsistEditorDialog dlg(sc, route_, cat_, sc.trains()[0], 0);
    QVERIFY(dlg.okButton()->isEnabled());

    // +10 вагонов (100 м) вытянут хвост до 50: пересечение с «Сосед»
    dlg.addVehicle("cars", "W", 10, 1.0);
    QVERIFY(!dlg.okButton()->isEnabled());
    QVERIFY(dlg.messagesText().contains("Сосед"));

    // Убрали лишнее - можно
    QVector<int> rows;
    for (int i = 4; i < 14; ++i)
    {
        rows << i;
    }

    dlg.removeUnits(rows);
    QVERIFY(dlg.okButton()->isEnabled());
}

//------------------------------------------------------------------------------
// Записанный состав читается каталогом обратно без потерь
//------------------------------------------------------------------------------
void TestConsist::saveTrainRoundTrip()
{
    QTemporaryDir game;
    QVERIFY(game.isValid());
    buildGame(game.path());

    VehicleCatalog cat;
    QVERIFY(cat.load(game.path()));

    TrainConfigData t = *cat.findTrain("poezd");
    t.name = "новый-1";
    t.title = "Состав <тест> & \"проба\"";
    t.description = "Описание";
    t.chargingPressure = 0.52;
    t.noAir = true;
    t.extraCommon.push_back({ "ClientName", "vl60k" });
    t.groups[1].count = 7;
    t.groups[1].payloadCoeff = 0.25;
    t.groups[1].forward = false;
    t.groups[1].brakeShoes = true;

    QString err;
    QVERIFY2(cat.saveTrain(t, false, &err), qPrintable(err));
    QVERIFY(QFileInfo::exists(cat.trainFilePath("новый-1")));

    // Повторная запись без overwrite - отказ, с overwrite - успех
    QVERIFY(!cat.saveTrain(t, false, &err));
    QVERIFY(err.contains("уже существует"));
    QVERIFY(cat.saveTrain(t, true, &err));

    // Временных файлов не осталось
    QVERIFY(!QFileInfo::exists(cat.trainFilePath("новый-1") + ".tmp"));

    VehicleCatalog again;
    QVERIFY(again.load(game.path()));

    const TrainConfigData *r = again.findTrain("новый-1");
    QVERIFY(r != nullptr);
    QCOMPARE(r->title, t.title);
    QCOMPARE(r->description, t.description);
    QVERIFY(qAbs(r->chargingPressure - 0.52) < 1e-9);
    QVERIFY(r->noAir);
    QCOMPARE(r->extraCommon.size(), 1);
    QCOMPARE(r->extraCommon[0].first, QString("ClientName"));
    QCOMPARE(r->groups.size(), 2);
    QCOMPARE(r->groups[1].count, 7);
    QVERIFY(qAbs(r->groups[1].payloadCoeff - 0.25) < 1e-9);
    QVERIFY(!r->groups[1].forward);
    QVERIFY(r->groups[1].brakeShoes);
    QCOMPARE(r->groups[0].module.id(), t.groups[0].module.id());
    QCOMPARE(r->groups[0].configDir, t.groups[0].configDir);

    // Исходный состав не тронут
    QVERIFY(again.findTrain("poezd") != nullptr);
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void TestConsist::saveTrainRejectsBadInput()
{
    TrainConfigData t = *cat_.findTrain("poezd");
    QString err;

    // Имена, которые нельзя использовать как имя файла
    for (const QString &bad : { QString(), QString("a/b"), QString("..\\x"), QString(".hidden"),
                               QString("a b"), QString("x:y") })
    {
        t.name = bad;
        QVERIFY2(!VehicleCatalog::isValidTrainName(bad), qPrintable(bad));
        QVERIFY(!cat_.saveTrain(t, true, &err));
    }

    QVERIFY(VehicleCatalog::isValidTrainName("vl60k-1737_T65.2+x"));

    // Без единиц
    t.name = "empty";
    t.groups.clear();
    QVERIFY(!cat_.saveTrain(t, true, &err));
    QVERIFY(err.contains("нет ни одной единицы"));

    // Единица без модуля
    t = *cat_.findTrain("poezd");
    t.name = "nomodule";
    t.groups[0].module = ModuleRef();
    QVERIFY(!cat_.saveTrain(t, true, &err));
    QVERIFY(err.contains("неизвестен модуль"));

    QVERIFY(!QFileInfo::exists(cat_.trainFilePath("nomodule")));
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void TestConsist::composerDialog()
{
    ConsistEditorDialog dlg(cat_, TrainConfigData());

    QCOMPARE(dlg.unitCount(), 0);
    QVERIFY(!dlg.okButton()->isEnabled());

    QVERIFY(dlg.addVehicle("locos", "L", 1, 0.0));
    QVERIFY(dlg.addVehicle("cars", "W", 5, 1.0));
    QVERIFY(dlg.addTrain("poezd", 1));
    QCOMPARE(dlg.unitCount(), 1 + 5 + 4);

    // Без имени файла сохранить нельзя
    QVERIFY(!dlg.okButton()->isEnabled());

    dlg.fileNameEdit()->setText("собранный");
    QVERIFY2(dlg.okButton()->isEnabled(), qPrintable(dlg.messagesText()));

    dlg.fileNameEdit()->setText("плохое имя/с слэшем");
    QVERIFY(!dlg.okButton()->isEnabled());
    dlg.fileNameEdit()->setText("собранный");

    // Равные подряд единицы склеены в группы
    const TrainConfigData cfg = dlg.trainConfig();
    QCOMPARE(cfg.name, QString("собранный"));
    QCOMPARE(cfg.groups.size(), 4);   // L, 5 W, L, 3 W(0%)
    QCOMPARE(cfg.groups[1].count, 5);

    // Правка готового состава: берутся его параметры
    TrainConfigData init = *cat_.findTrain("poezd");
    ConsistEditorDialog edit(cat_, init);
    QCOMPARE(edit.unitCount(), 4);
    QCOMPARE(edit.fileNameEdit()->text(), QString("poezd"));
    QVERIFY(edit.okButton()->isEnabled());
}

//------------------------------------------------------------------------------
// Из окна: «Новый состав…» → диалог → файл в cfg/trains → состав в списке
//------------------------------------------------------------------------------
void TestConsist::mainWindowComposesAndSavesTrain()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    buildGame(root.path());
    buildRoute(root.path() + "/routes/r");

    MainWindow w;
    QVERIFY(w.openRoute(root.path() + "/routes/r"));
    QVERIFY(w.catalog().isLoaded());
    QCOMPARE(w.catalog().trains().size(), 1);

    QTimer::singleShot(50, [&]
    {
        auto *dlg = qobject_cast<ConsistEditorDialog *>(QApplication::activeModalWidget());
        QVERIFY(dlg != nullptr);

        dlg->addVehicle("locos", "L", 1, 0.0);
        dlg->addVehicle("cars", "W", 3, 0.5);
        dlg->fileNameEdit()->setText("сценарный-1");
        dlg->okButton()->click();
    });

    w.composeTrain(QString(), false);

    QCOMPARE(w.catalog().trains().size(), 2);

    const TrainConfigData *t = w.catalog().findTrain("сценарный-1");
    QVERIFY(t != nullptr);
    QCOMPARE(t->groups.size(), 2);
    QCOMPARE(t->groups[1].count, 3);
    QVERIFY(QFileInfo::exists(root.path() + "/cfg/trains/сценарный-1.xml"));

    // Правка готового состава «как копия» не трогает исходный файл
    QTimer::singleShot(50, [&]
    {
        auto *dlg = qobject_cast<ConsistEditorDialog *>(QApplication::activeModalWidget());
        QVERIFY(dlg != nullptr);
        QCOMPARE(dlg->fileNameEdit()->text(), QString("poezd-copy"));
        dlg->removeUnits({ 3 });
        dlg->okButton()->click();
    });

    w.composeTrain("poezd", true);

    QCOMPARE(w.catalog().trains().size(), 3);
    QCOMPARE(w.catalog().findTrain("poezd")->groups[1].count, 3);
    QCOMPARE(w.catalog().findTrain("poezd-copy")->groups[1].count, 2);
}

void TestConsist::mainWindowExportsAndReopensProject()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    buildGame(root.path());
    buildRoute(root.path() + "/routes/r");

    MainWindow w;
    QVERIFY(w.openRoute(root.path() + "/routes/r"));

    PlacedTrain t = trainFrom(w.catalog(), "poezd", "T", 200.0, 1);
    t.name = "P1";
    t.autoMode = true;

    // Состав, которого нет в cfg/trains
    PlacedTrain u = t;
    u.name = "P2";
    u.sourceTrain.clear();
    u.groups.last().count = 2;
    updateSummary(u, w.catalog());
    u.coord = 390.0;
    u.autoMode = false;

    const_cast<Scenario &>(w.scenario()).add(t);
    const_cast<Scenario &>(w.scenario()).add(u);

    ExportOptions opt;
    opt.info.scenarioName = "demo";
    QString err;
    QVERIFY2(w.exportScenarioTo(opt, &err), qPrintable(err));

    QVERIFY(QFileInfo::exists(root.path() + "/routes/r/scenarios/demo/main.lua"));
    QVERIFY(QFileInfo::exists(root.path() + "/cfg/trains/demo-P2.xml"));
    // каталог перечитан
    QVERIFY(w.catalog().findTrain("demo-P2") != nullptr);

    // Проект: сохранить, открыть в другом окне
    QVERIFY(w.isModified());
    const QString proj = root.path() + "/p.rrsproj";
    QVERIFY(w.saveProjectAs(proj));
    QVERIFY(!w.isModified());
    QCOMPARE(w.projectInfo().scenarioName, QString("demo"));

    MainWindow w2;
    QVERIFY(!w2.isModified());
    QVERIFY(w2.openProject(proj));
    QVERIFY(!w2.isModified());
    QCOMPARE(w2.scenario().size(), 2);
    QCOMPARE(w2.scenario().trains()[0].name, QString("P1"));
    QVERIFY(w2.scenario().trains()[0].autoMode);
    QVERIFY(qAbs(w2.scenario().trains()[1].length - 40.0) < 1e-6);
    QCOMPARE(w2.projectInfo().scenarioName, QString("demo"));
    QCOMPARE(w2.trainsPanel()->rowCount(), 2);
    w2.removeTrain(0);
    QVERIFY(w2.isModified());
}

void TestConsist::mainWindowEventsWithMapPick()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    buildGame(root.path());
    buildRoute(root.path() + "/routes/r");

    MainWindow w;
    QVERIFY(w.openRoute(root.path() + "/routes/r"));

    // Диалог: добавить действие, поле «От траектории» взять с карты
    QTimer::singleShot(50, [&]
    {
        auto *dlg = qobject_cast<EventDialog *>(QApplication::activeModalWidget());
        QVERIFY(dlg != nullptr);
        dlg->addButton()->click();
        dlg->fieldB()->setEditText("S");
        // «На карте» для поля A
        auto buttons = dlg->findChildren<QPushButton *>();
        for (QPushButton *b : buttons)
        {
            if (b->text() == "На карте…" && b->isVisibleTo(dlg) && b->parentWidget()->parentWidget()->objectName().isEmpty()
                && b->parentWidget()->layout()->indexOf(dlg->fieldA()) >= 0)
            {
                b->click();
                return;
            }
        }
        QFAIL("кнопка не найдена");
    });

    w.addEvent();
    QVERIFY(w.picking());
    QVERIFY(w.map()->pickMode());

    Route::Hit hit;
    hit.valid = true;
    hit.trajectory = "T";
    hit.coord = 10.0;

    // Вернулись в диалог с подставленной траекторией; подтверждаем
    QTimer::singleShot(50, [&]
    {
        auto *dlg = qobject_cast<EventDialog *>(QApplication::activeModalWidget());
        QVERIFY(dlg != nullptr);
        QCOMPARE(dlg->fieldA()->currentText(), QString("T"));
        QCOMPARE(dlg->fieldB()->currentText(), QString("S"));
        QVERIFY(dlg->okButton()->isEnabled());
        dlg->okButton()->click();
    });

    w.placeAt(hit);

    QVERIFY(!w.picking());
    QCOMPARE(w.projectInfo().events.size(), 1);
    QCOMPARE(w.projectInfo().events[0].actions[0].a, QString("T"));
    QCOMPARE(w.eventsPanel()->rowCount(), 1);

    w.moveEvent(0, 1);
    w.removeEvent(0);
    QCOMPARE(w.projectInfo().events.size(), 0);
}

void TestConsist::mainWindowImportsScenario()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    buildGame(root.path());
    buildRoute(root.path() + "/routes/r");

    const QString sc = root.path() + "/routes/r/scenarios/old";
    QDir().mkpath(sc);
    QFile f(sc + "/main.lua");
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("setTime(\"10:00\")\ntr = TrainData.new()\ntr.name = \"A\"\ntr.config = \"poezd\"\n"
            "tr.traj = \"T\"\ntr.coord = 200.0\ntr.dir = 1\ntr.auto = true\nsetTrain(tr)\n"
            "setTimeTrigger(\"10:05\", actionReverseTrain(\"A\"))\nlogMessage(\"x\")\n");
    f.close();

    MainWindow w;
    QVERIFY(w.openRoute(root.path() + "/routes/r"));

    QStringList report;
    QVERIFY(w.importScenarioDir(sc, false, &report));
    QCOMPARE(w.scenario().size(), 1);
    QCOMPARE(w.scenario().trains()[0].name, QString("A"));
    QCOMPARE(w.projectInfo().events.size(), 1);
    QCOMPARE(w.projectInfo().opaquePost.size(), 1);
    QCOMPARE(w.projectInfo().scenarioName, QString("old"));
    QCOMPARE(w.projectInfo().startTime, 10 * 3600);
    QCOMPARE(w.trainsPanel()->rowCount(), 1);
    QCOMPARE(w.eventsPanel()->rowCount(), 1);

    // Правим и экспортируем поверх (с заменой)
    ExportOptions opt;
    opt.info = w.projectInfo();
    opt.overwrite = true;
    QString err;
    QVERIFY2(w.exportScenarioTo(opt, &err), qPrintable(err));

    QFile g(sc + "/main.lua");
    QVERIFY(g.open(QIODevice::ReadOnly));
    const QString lua = QString::fromUtf8(g.readAll());
    QVERIFY(lua.contains("tr.name = \"A\""));
    QVERIFY(lua.contains("logMessage(\"x\")"));
    QVERIFY(lua.contains("setTimeTrigger(\"10:05:00\", event1_actions)"));
}

void TestConsist::continuationFlow()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    buildGame(root.path());
    buildRoute(root.path() + "/routes/r");

    MainWindow w;
    QVERIFY(w.openRoute(root.path() + "/routes/r"));
    auto &sc = const_cast<Scenario &>(w.scenario());

    PlacedTrain wagons = trainFrom(w.catalog(), "poezd", "T", 380.0, 1);
    wagons.name = "W";
    sc.add(wagons);

    PlacedTrain loco = trainFrom(w.catalog(), "poezd", "T", 100.0, 1);
    loco.name = "L";
    loco.autoMode = true;
    sc.add(loco);

    auto makeTimetable = [](int from, int to, double c0, double c1, const QString &first, const QString &last)
    {
        Timetable tt;
        tt.valid = true;
        tt.startTime = from;
        tt.endTrajectory = "T";
        tt.endCoord = c1;
        const int dir = c1 >= c0 ? 1 : -1;

        for (int t = from; t <= to; t += 30)
        {
            TimetableSample sm;
            sm.time = t;
            sm.trajectory = "T";
            sm.coord = c0 + (c1 - c0) * double(t - from) / double(to - from);
            sm.dir = dir;
            tt.samples.push_back(sm);
        }

        TimetableStation a;
        a.name = first;
        a.arrival = a.departure = from;
        a.targetTrajectory = "T";
        a.targetCoord = c0;
        TimetableStation b;
        b.name = last;
        b.arrival = b.departure = to;
        b.targetTrajectory = "T";
        b.targetCoord = c1;
        tt.stations << a << b;

        return tt;
    };

    const int T0 = 12 * 3600;
    w.setTimetable(1, makeTimetable(T0, T0 + 300, 100.0, 325.0, "A", "B"));

    // Продолжение через диалог
    PlacedTrain seed;
    seed.formedFrom = "L";
    seed.name = "Out";
    seed.formedWagons = "W";

    ContinuationDialog dlg(w.route(), w.scenario(), 1, seed, -1);
    QVERIFY2(dlg.okButton()->isEnabled(), qPrintable(dlg.infoText()));
    const PlacedTrain cont = dlg.result();
    QVERIFY(cont.isContinuation());
    QCOMPARE(cont.dir, -1);
    QCOMPARE(cont.coord, 275.0);
    QVERIFY(qAbs(cont.length - 100.0) < 1e-6);

    sc.add(cont);
    w.setTimetable(2, makeTimetable(T0 + 600, T0 + 900, 275.0, 0.0, "B", "C"));
    QVERIFY(w.scenario().trains()[2].timetable.valid);

    // Проверка графиков: чисто
    for (const ScheduleIssue &i : w.checkResult())
    {
        QVERIFY2(i.severity == IssueSeverity::Info, qPrintable(i.kind + ": " + i.text));
    }

    // Окно проверки: чисто; после порчи графика появляются строки
    {
        CheckDialog cd([&w] { return w.checkResult(); });
        QCOMPARE(cd.shownCount(), 0);

        PlacedTrain bad = w.scenario().trains()[2];
        bad.timetable.startTime = T0;          // раньше сцепки
        bad.timetable.stations[0].departure = T0;
        const_cast<Scenario &>(w.scenario()).replace(2, bad);
        cd.recheck();
        QVERIFY(cd.shownCount() >= 1);
        QVERIFY(cd.reportText().contains("ОШИБКА"));

        bad.timetable.startTime = T0 + 600;
        bad.timetable.stations[0].departure = T0 + 600;
        const_cast<Scenario &>(w.scenario()).replace(2, bad);
        cd.recheck();
        QCOMPARE(cd.shownCount(), 0);
    }

    // Экспорт: локомотив раньше вагонов, у продолжения нет setTrain, есть график и переименование
    ExportOptions opt;
    opt.info.scenarioName = "chain";
    QString err;
    QVERIFY2(w.exportScenarioTo(opt, &err), qPrintable(err));

    QFile f(root.path() + "/routes/r/scenarios/chain/main.lua");
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QString lua = QString::fromUtf8(f.readAll());
    QVERIFY(lua.indexOf("\"L\"") >= 0 && lua.indexOf("\"L\"") < lua.indexOf("\"W\""));
    QVERIFY2(lua.contains("renameTrain(\"L\", \"Out\")"), qPrintable(lua));
    QVERIFY(lua.contains("reverseTrain(\"Out\")"));
    QVERIFY(lua.contains("12:07:00"));
    QVERIFY(!lua.contains(".name = \"Out\""));
    QVERIFY(QFileInfo::exists(root.path() + "/routes/r/scenarios/chain/timetable/Out.xml"));
    QVERIFY(QFileInfo::exists(root.path() + "/routes/r/scenarios/chain/timetable/L.xml"));

    // Проект: продолжение сохраняется
    const QString proj = root.path() + "/c.rrsproj";
    QVERIFY(w.saveProjectAs(proj));
    MainWindow w2;
    QVERIFY(w2.openProject(proj));
    QVERIFY(w2.scenario().trains()[2].isContinuation());
    QCOMPARE(w2.scenario().trains()[2].formedWagons, QString("W"));
    QVERIFY(w2.scenario().trains()[2].timetable.valid);

    // Локомотив прибывает в другую точку: отправная точка продолжения сдвигается, график сбрасывается
    w.setTimetable(1, makeTimetable(T0, T0 + 300, 100.0, 320.0, "A", "B"));
    QCOMPARE(w.scenario().trains()[2].coord, 270.0);
    QVERIFY(!w.scenario().trains()[2].timetable.valid);

    // Удаление вагонов уносит продолжение
    w.removeTrain(0);
    QCOMPARE(w.scenario().size(), 1);
    QCOMPARE(w.scenario().trains()[0].name, QString("L"));
}

void TestConsist::dragAndRelocateTrain()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    buildGame(root.path());
    buildRoute(root.path() + "/routes/r");

    MainWindow w;
    w.resize(900, 700);
    w.show();
    QVERIFY(w.openRoute(root.path() + "/routes/r"));

    PlacedTrain t = trainFrom(w.catalog(), "poezd", "T", 200.0, 1);
    t.name = "P1";
    const_cast<Scenario &>(w.scenario()).add(t);
    w.trainsPanel()->refresh();
    w.trainsPanel()->setCurrentIndex(0);

    // Перенос программно: на другой путь, пересечение и тупик запрещены
    QVERIFY(w.moveTrain(0, "T", 300.0, 1));
    QCOMPARE(w.scenario().trains()[0].coord, 300.0);
    QVERIFY(!w.moveTrain(0, "S", 10.0, 1));          // тупик 25 м, состав 50 м
    QCOMPARE(w.scenario().trains()[0].trajectory, QString("T"));

    PlacedTrain other = t;
    other.name = "P2";
    other.coord = 100.0;
    const_cast<Scenario &>(w.scenario()).add(other);
    QVERIFY(!w.moveTrain(0, "T", 120.0, 1));         // пересекается с P2
    QCOMPARE(w.scenario().trains()[0].coord, 300.0);

    // Перетаскивание мышью: 1 м = 1 пиксель, голова P1 (координата 300) в центре окна
    w.map()->viewAt(QPointF(0.0, 300.0), 1.0);
    QTest::qWait(20);
    const QPoint c(w.map()->width() / 2, w.map()->height() / 2);

    // Состав занимает 50 м позади головы: цепляемся за точку 10 м позади
    bool moved = false;

    auto send = [&](QEvent::Type type, const QPoint &pos, Qt::MouseButton btn, Qt::MouseButtons btns)
    {
        QMouseEvent ev(type, QPointF(pos), QPointF(w.map()->mapToGlobal(pos)), btn, btns, Qt::NoModifier);
        QApplication::sendEvent(w.map(), &ev);
    };

    // карта открыта с поворотом на 90°: ось пути лежит горизонтально, рост координаты - вправо;
    // точка 10 м позади головы (мир 290) - левее центра
    const QPoint from = c + QPoint(-10, 0);
    send(QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton);
    send(QEvent::MouseMove, from + QPoint(-40, 0), Qt::NoButton, Qt::LeftButton);
    send(QEvent::MouseMove, from + QPoint(-80, 0), Qt::NoButton, Qt::LeftButton);
    send(QEvent::MouseButtonRelease, from + QPoint(-80, 0), Qt::LeftButton, Qt::NoButton);
    moved = qAbs(w.scenario().trains()[0].coord - 300.0) > 20.0;

    QVERIFY(moved);
    QCOMPARE(w.scenario().trains()[0].trajectory, QString("T"));
    QVERIFY(w.scenario().trains()[0].coord > 150.0);   // не наехал на P2

    // Диалог положения: перенос на другой путь через список путей
    {
        PlaceTrainDialog dlg(w.route(), w.scenario(), w.scenario().trains()[0], 0);
        QCOMPARE(dlg.waypointBox()->count(), 1 + w.route().waypoints().size());
        dlg.trajectoryBox()->setEditText("S");
        QCOMPARE(dlg.coordBox()->maximum(), w.route().find("S")->length);
        QCOMPARE(dlg.result().trajectory, QString("S"));
        dlg.trajectoryBox()->setEditText("T");
        QCOMPARE(dlg.result().trajectory, QString("T"));
        QVERIFY(dlg.okButton()->isEnabled());
    }

}

QTEST_MAIN(TestConsist)

#include "test-consist.moc"
