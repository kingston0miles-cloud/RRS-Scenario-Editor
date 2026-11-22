#include    <QtTest>

#include    <QDir>
#include    <QFile>
#include    <QTemporaryDir>

#include    "scenario/consist.h"
#include    "scenario/events.h"
#include    "scenario/export.h"

using namespace rse;

namespace
{

void writeFile(const QString &path, const QString &content)
{
    QDir().mkpath(QFileInfo(path).absolutePath());

    QFile f(path);
    QVERIFY2(f.open(QIODevice::WriteOnly), qPrintable(path));
    f.write(content.toUtf8());
}

QString readFile(const QString &path)
{
    QFile f(path);

    if (!f.open(QIODevice::ReadOnly))
    {
        return QString();
    }

    return QString::fromUtf8(f.readAll());
}

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

} // namespace

class TestExport : public QObject
{
    Q_OBJECT

private:

    QTemporaryDir dir_;
    Route route_;
    VehicleCatalog catalog_;
    Scenario scenario_;

    QString routeDir() const
    {
        return dir_.path() + "/routes/r";
    }

    PlacedTrain make(const QString &name, double coord, bool from_source)
    {
        PlacedTrain t;
        const TrainConfigData *cfg = catalog_.findTrain("poezd");

        t.name = name;
        t.title = name;
        t.groups = cfg->groups;
        t.sourceTrain = from_source ? "poezd" : QString();
        t.trajectory = "T";
        t.coord = coord;
        t.dir = 1;
        updateSummary(t, catalog_);

        return t;
    }

private slots:

    void initTestCase()
    {
        QVERIFY(dir_.isValid());
        const QString root = dir_.path();

        writeFile(root + "/cfg/vehicles/locos/L.xml", vehicleXml(20.0, 100000.0, 0.0, true, true));
        writeFile(root + "/cfg/vehicles/cars/W.xml", vehicleXml(10.0, 10000.0, 50000.0, false, false));
        writeFile(root + "/cfg/trains/poezd.xml",
                  "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<Config>\n<Common>\n"
                  "<ChargingPressure>0.5</ChargingPressure>\n<Title>t</Title>\n</Common>\n"
                  + groupXml("loco", "l", "locos", "L", 1) + groupXml("car", "w", "cars", "W", 3)
                  + "</Config>\n");

        writeFile(routeDir() + "/topology/trajectories/T.traj", "0 0 0 0\n0 400 0 400\n");
        writeFile(routeDir() + "/topology/trajectories/U.traj", "0 400 0 400\n0 800 0 800\n");
        writeFile(routeDir() + "/topology/topology.xml",
                  "<Config><Switch><Name>s</Name><bwdPlusTraj>T</bwdPlusTraj>"
                  "<fwdPlusTraj>U</fwdPlusTraj></Switch></Config>\n");

        QVERIFY2(route_.load(routeDir()), qPrintable(route_.errorString()));
        QVERIFY2(catalog_.load(root), qPrintable(catalog_.errorString()));

        PlacedTrain a = make("Поезд 1", 100.0, true);
        a.autoMode = true;
        a.timetable.valid = true;
        a.timetable.startTime = 12 * 3600;
        a.timetable.endTrajectory = "U";
        a.timetable.endCoord = 300.0;

        auto station = [](const QString &n, int arr, int dep, const QString &t, double c)
        {
            TimetableStation s;
            s.name = n;
            s.arrival = arr;
            s.departure = dep;
            s.targetTrajectory = t;
            s.targetCoord = c;
            return s;
        };

        a.timetable.stations = { station("A", 12 * 3600, 12 * 3600, "T", 100.0),
                                 station("B", 12 * 3600 + 600, 12 * 3600 + 660, "T", 380.0),
                                 station("C", 12 * 3600 + 1200, 12 * 3600 + 1200, "U", 300.0) };
        scenario_.add(a);

        PlacedTrain b = make("Состав B", 380.0, false);
        // отредактированный состав: убрали один вагон
        b.groups.last().count = 2;
        updateSummary(b, catalog_);
        b.coord = 700.0;
        b.trajectory = "U";
        scenario_.add(b);
    }

    void planAndWrite()
    {
        ExportOptions opt;
        opt.info.scenarioName = "demo";
        opt.info.description = "Описание сценария";

        const ExportPlan plan = planExport(route_, catalog_, scenario_, opt);
        QVERIFY2(plan.ok(), qPrintable(plan.errors.join("; ")));
        QCOMPARE(plan.trains.size(), 2);
        QCOMPARE(plan.trains[0].configName, QString("poezd"));
        QVERIFY(!plan.trains[0].writeConfig);
        QCOMPARE(plan.trains[1].configName, QString("demo-B"));
        QVERIFY(plan.trains[1].writeConfig);
        QCOMPARE(plan.startTime, 12 * 3600 - 180);

        QString err;
        QVERIFY2(writeExport(route_, catalog_, scenario_, plan, opt, &err), qPrintable(err));

        const QString lua = readFile(routeDir() + "/scenarios/demo/main.lua");
        QVERIFY(lua.contains("setTime(\"11:57\")"));
        QVERIFY(lua.contains("train1.config = \"poezd\""));
        QVERIFY(lua.contains("train1.auto = true"));
        QVERIFY(lua.contains("train2.config = \"demo-B\""));
        QVERIFY(lua.contains("train2.name = \"Состав B\""));
        QVERIFY(lua.contains("train2.auto = false"));
        QVERIFY(lua.contains("train2.dir = 1"));
        QVERIFY(lua.contains("setTrain(train2)"));

        // Расписание: у первой станции нет прибытия, у последней - отправления
        const QString xml = readFile(routeDir() + "/scenarios/demo/timetable/Поезд 1.xml");
        QVERIFY(xml.contains("<Name>A</Name>"));
        QVERIFY(xml.contains("<ArrivalTime>12:10</ArrivalTime>"));
        QVERIFY(xml.contains("<DepartureTime>12:11</DepartureTime>"));
        QCOMPARE(xml.count("<ArrivalTime>"), 2);
        QCOMPARE(xml.count("<DepartureTime>"), 2);
        QVERIFY(xml.contains("<TargetTraj>U</TargetTraj>"));
        QVERIFY(xml.contains("<TargetCoord>300.0</TargetCoord>"));
        // Станция B на T, дальше путь идёт на U
        QVERIFY(xml.contains("<RemovalTraj>U</RemovalTraj>"));

        QVERIFY(readFile(routeDir() + "/scenarios/demo/README.md").contains("Описание сценария"));

        // Новый состав записан и читается заново
        const TrainConfigData *cfg = nullptr;
        VehicleCatalog again;
        QVERIFY(again.load(dir_.path()));
        cfg = again.findTrain("demo-B");
        QVERIFY(cfg != nullptr);
        QCOMPARE(again.summarize(cfg->groups).vehicles, 3);

        // Повторный экспорт без замены не проходит, с заменой - да, составы не дублируются
        const ExportPlan p2 = planExport(route_, again, scenario_, opt);
        QVERIFY(!p2.ok());
        opt.overwrite = true;
        const ExportPlan p3 = planExport(route_, again, scenario_, opt);
        QVERIFY2(p3.ok(), qPrintable(p3.errors.join("; ")));
        QVERIFY(!p3.trains[1].writeConfig);
        QCOMPARE(p3.trains[1].configName, QString("demo-B"));
    }

    void problems()
    {
        ExportOptions opt;
        opt.info.scenarioName = "bad name";
        QVERIFY(!planExport(route_, catalog_, scenario_, opt).ok());

        opt.info.scenarioName = "ok2";

        Scenario sc = scenario_;
        PlacedTrain dup = sc.trains()[0];
        dup.name = sc.trains()[1].name;
        dup.coord = 50.0;
        sc.add(dup);
        ExportPlan p = planExport(route_, catalog_, sc, opt);
        QVERIFY(!p.ok());
        QVERIFY(p.errors.join("\n").contains("повторяется"));

        // Бот без автопилота и график без бота - предупреждения
        Scenario s2;
        PlacedTrain w = scenario_.trains()[1];
        w.autoMode = true;
        w.autopilotCapable = false;
        s2.add(w);
        p = planExport(route_, catalog_, s2, opt);
        QVERIFY(p.ok());
        QVERIFY(!p.warnings.isEmpty());

        QVERIFY(!isValidScenarioName(""));
        QVERIFY(isValidScenarioName("my-scenario_1"));
    }

    void eventsLuaAndChecks()
    {
        ScenarioEvent e1;
        e1.title = "Отправление";
        e1.trigger = TriggerType::AtTime;
        e1.time = 12 * 3600 + 600;
        e1.actions.push_back({ ActionType::BuildTrainRoute, "T", "U", 1 });
        e1.actions.push_back({ ActionType::OpenSignal, "s", "", 1 });
        e1.actions.push_back({ ActionType::SwitchFwd, "s", "", 1 });

        ScenarioEvent e2;
        e2.title = "Разворот";
        e2.trigger = TriggerType::TrajBusy;
        e2.traj = "U";
        e2.train = "Поезд 1";
        e2.delaySec = 80;
        e2.actions.push_back({ ActionType::ReverseTrain, "Поезд 1", "", 1 });
        e2.actions.push_back({ ActionType::RenameTrain, "Поезд 1", "Обратный", 1 });

        ScenarioEvent e3;
        e3.title = "Пропуск";
        e3.trigger = TriggerType::AutoApproach;
        e3.traj = "T";
        e3.traj2 = "U";
        e3.dir = 1;

        ScenarioEvent e4;
        e4.trigger = TriggerType::AfterStart;
        e4.time = 10;
        e4.actions.push_back({ ActionType::CloseSignal, "nope", "", -1 });

        ScenarioEvent e5;
        e5.trigger = TriggerType::TrajFree;
        e5.traj = "T";

        const int start = 12 * 3600 - 180;
        QVERIFY(checkEvent(route_, scenario_, e1, start).errors.isEmpty());
        QVERIFY(checkEvent(route_, scenario_, e1, start).warnings.isEmpty());
        QVERIFY(checkEvent(route_, scenario_, e2, start).errors.isEmpty());
        QVERIFY(checkEvent(route_, scenario_, e3, start).warnings.isEmpty());
        QVERIFY(!checkEvent(route_, scenario_, e4, start).warnings.isEmpty());   // нет стрелки
        QVERIFY(!checkEvent(route_, scenario_, e5, start).errors.isEmpty());     // нет действий

        ScenarioEvent back = e3;
        back.traj = "U";
        back.traj2 = "T";
        QVERIFY(!checkEvent(route_, scenario_, back, start).warnings.isEmpty()); // пути нет

        const QString lua = luaEvents({ e1, e2, e3, e4 });
        QVERIFY(lua.contains("function event1_actions()"));
        QVERIFY(lua.contains("\tbuildTrainRoute(\"T\", \"U\", 1)"));
        QVERIFY(lua.contains("\topenSignal(\"s\", 1)"));
        QVERIFY(lua.contains("\tswitchFwd(\"s\")"));
        QVERIFY(lua.contains("setTimeTrigger(\"12:10:00\", event1_actions)"));
        QVERIFY(lua.contains("setPostEventTimeTrigger(\"+00:01:20\", event2_actions)"));
        QVERIFY(lua.contains("setOnTrajBusyByTrainTrigger(\"U\", \"Поезд 1\", event2_fire)"));
        QVERIFY(lua.contains("\trenameTrain(\"Поезд 1\", \"Обратный\")"));
        QVERIFY(lua.contains("setAutoApproachTrigger(\"T\", \"U\", 1)"));
        QVERIFY(lua.contains("setTimeTrigger(\"+00:00:10\", event4_actions)"));
        QVERIFY(lua.contains("\tcloseSignal(\"nope\", -1)"));

        // Экспорт: события попадают в main.lua, ошибка события блокирует экспорт
        ExportOptions opt;
        opt.info.scenarioName = "withev";
        opt.info.events = { e1, e2, e3 };
        ExportPlan plan = planExport(route_, catalog_, scenario_, opt);
        QVERIFY2(plan.ok(), qPrintable(plan.errors.join("; ")));
        QString err;
        QVERIFY2(writeExport(route_, catalog_, scenario_, plan, opt, &err), qPrintable(err));
        const QString main = readFile(routeDir() + "/scenarios/withev/main.lua");
        QVERIFY(main.contains("setPostEventTimeTrigger"));
        QVERIFY(main.indexOf("setTrain(train2)") < main.indexOf("function event1_actions"));
        QVERIFY(readFile(routeDir() + "/scenarios/withev/README.md").contains("### События"));

        opt.info.scenarioName = "withev2";
        opt.info.events.push_back(e5);
        QVERIFY(!planExport(route_, catalog_, scenario_, opt).ok());

        // Проект хранит события
        ProjectInfo info;
        info.events = { e1, e2, e3, e4 };
        const QString path = dir_.path() + "/ev.rrsproj";
        QVERIFY(saveProject(path, info, scenario_, &err));
        ProjectInfo got;
        Scenario sc;
        QVERIFY(loadProject(path, &got, &sc, &err));
        QCOMPARE(got.events.size(), 4);
        QCOMPARE(got.events[1].delaySec, 80);
        QCOMPARE(got.events[1].train, QString("Поезд 1"));
        QCOMPARE(got.events[0].actions.size(), 3);
        QCOMPARE(got.events[0].actions[1].type, ActionType::OpenSignal);
        QCOMPARE(got.events[3].actions[0].dir, -1);
        QCOMPARE(got.events[2].traj2, QString("U"));
    }

    void projectRoundTrip()
    {
        ProjectInfo info;
        info.scenarioName = "demo";
        info.description = "Текст";
        info.startTime = 43200;
        info.routeDir = routeDir();
        info.gameRoot = dir_.path();

        const QString path = dir_.path() + "/work/p.rrsproj";
        QDir().mkpath(dir_.path() + "/work");

        QString err;
        QVERIFY2(saveProject(path, info, scenario_, &err), qPrintable(err));

        ProjectInfo got;
        Scenario sc;
        QVERIFY2(loadProject(path, &got, &sc, &err), qPrintable(err));

        QCOMPARE(got.scenarioName, info.scenarioName);
        QCOMPARE(got.description, info.description);
        QCOMPARE(got.startTime, 43200);
        QCOMPARE(QDir(got.routeDir).canonicalPath(), QDir(routeDir()).canonicalPath());
        QCOMPARE(sc.size(), 2);

        const PlacedTrain &a = sc.trains()[0];
        const PlacedTrain &o = scenario_.trains()[0];
        QCOMPARE(a.name, o.name);
        QCOMPARE(a.groups.size(), o.groups.size());
        QCOMPARE(a.groups[1].count, 3);
        QCOMPARE(a.coord, o.coord);
        QCOMPARE(a.autoMode, true);
        QVERIFY(a.timetable.valid);
        QCOMPARE(a.timetable.stations.size(), 3);
        QCOMPARE(a.timetable.stations[1].departure, o.timetable.stations[1].departure);
        QVERIFY(!sc.trains()[1].timetable.valid);
        QCOMPARE(sc.trains()[1].groups.last().count, 2);

        QCOMPARE(QDir(projectRouteDir(path)).canonicalPath(), QDir(routeDir()).canonicalPath());

        // Файл не проекта
        writeFile(dir_.path() + "/x.json", "{\"a\":1}");
        QVERIFY(!loadProject(dir_.path() + "/x.json", &got, &sc, &err));
        QVERIFY(!err.isEmpty());
    }
};

QTEST_MAIN(TestExport)
#include "test-export.moc"
