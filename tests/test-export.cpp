#include    <QtTest>

#include    <QCryptographicHash>
#include    <QDir>
#include    <QFile>
#include    <QTemporaryDir>

#include    "scenario/consist.h"
#include    "scenario/events.h"
#include    "scenario/export.h"
#include    "test-utils.h"

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

    void plannedTrainOnlyTimetable()
    {
        Scenario sc = scenario_;
        PlacedTrain t = sc.trains()[0];
        t.planned = true;
        sc.replace(0, t);

        ExportOptions opt;
        opt.info.scenarioName = "demo2";

        const ExportPlan plan = planExport(route_, catalog_, sc, opt);
        QVERIFY2(plan.ok(), qPrintable(plan.errors.join("; ")));
        QCOMPARE(plan.trains.size(), 1);
        QVERIFY(plan.files.contains("timetable/Поезд 1.xml"));
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

        // Пакет-дополнение (этап 29): минимальное дерево игры поверху
        opt.addonDir = dir_.path() + "/addon";
        const ExportPlan p4 = planExport(route_, again, scenario_, opt);
        QVERIFY2(p4.ok(), qPrintable(p4.errors.join("; ")));
        QVERIFY2(writeExport(route_, again, scenario_, p4, opt, &err), qPrintable(err));

        const QString pkg = dir_.path() + "/addon/routes/r/scenarios/demo";
        QVERIFY(QFile::exists(pkg + "/main.lua"));
        QVERIFY(QFile::exists(pkg + "/README.md"));
        QVERIFY(QFile::exists(pkg + "/timetable/Поезд 1.xml"));
        QVERIFY(QFile::exists(dir_.path() + "/addon/cfg/trains/demo-B.xml"));
        QVERIFY(QFile::exists(dir_.path() + "/addon/README-package.md"));
        QVERIFY(readFile(pkg + "/main.lua") == readFile(routeDir() + "/scenarios/demo/main.lua"));
        QVERIFY(readFile(pkg + "/timetable/Поезд 1.xml")
                == readFile(routeDir() + "/scenarios/demo/timetable/Поезд 1.xml"));

        // Платформы станции попадают в XML расписания (этап 27)
        Scenario pl = scenario_;
        PlacedTrain pt = pl.trains()[0];
        QVERIFY(!pt.timetable.stations.isEmpty());
        pt.timetable.stations[0].rightPlatform = 1;
        pt.timetable.stations[0].leftPlatform = -1;
        pl.replace(0, pt);
        const ExportPlan p5 = planExport(route_, again, pl, opt);
        QVERIFY2(p5.ok(), qPrintable(p5.errors.join("; ")));
        QVERIFY2(writeExport(route_, again, pl, p5, opt, &err), qPrintable(err));
        const QString plxml = readFile(routeDir() + "/scenarios/demo/timetable/Поезд 1.xml");
        QVERIFY(plxml.contains("<IsRightPlatform>1</IsRightPlatform>"));
        QVERIFY(!plxml.contains("<IsLeftPlatform>"));
    }

    void timetableKeepsApproachWithOrdering()
    {
        // Два поезда одной категории идут по одному пути со сдвигом в минуту:
        // редактор строит события очерёдности, но подход и уход в расписании
        // сохраняются. Движок строит маршруты по графику для любого поезда -
        // и с автоведением, и без него, поэтому поля нельзя стирать
        Scenario sc = scenario_;
        PlacedTrain first = sc.trains()[0];
        first.name = "Поезд 2";
        // Встаёт дальше по тому же пути, чтобы габариты не пересекались
        first.coord = 240.0;
        first.timetable.stations[0].targetCoord = 240.0;

        for (TimetableStation &st : first.timetable.stations)
        {
            st.arrival -= 60;
            st.departure -= 60;
        }

        sc.add(first);

        ExportOptions opt;
        opt.info.scenarioName = "demo-order";
        opt.overwrite = true;

        const ExportPlan plan = planExport(route_, catalog_, sc, opt);
        QVERIFY2(plan.ok(), qPrintable(plan.errors.join("; ")));
        QVERIFY2(writeExport(route_, catalog_, sc, plan, opt, nullptr), "writeExport");

        // Предусловие: между поездами действительно построена очерёдность
        const QString lua = readFile(routeDir() + "/scenarios/demo-order/main.lua");
        QVERIFY(lua.contains("Очерёдность"));

        const QString xml = readFile(routeDir() + "/scenarios/demo-order/timetable/Поезд 1.xml");
        QVERIFY(xml.contains("<ApproachTraj>"));
        QVERIFY(xml.contains("<RemovalTraj>"));
        // Ручные значения станций не теряются и совпадают с расчётом
        QVERIFY(xml.contains("<RemovalTraj>U</RemovalTraj>"));
    }

    void nonBotTimetableKeepsMandatoryRouteEvents()
    {
        // В RRS нормативный график обрабатывается компонентами Autopilot.
        // Для обычного поезда редактор обязан экспортировать Lua-события,
        // даже если пользователь отключил дополнительные триггеры для ботов.
        Scenario sc;
        PlacedTrain t = scenario_.trains()[0];
        t.autoMode = false;
        t.noAutoEvents = false;
        sc.add(t);

        ExportOptions opt;
        opt.info.scenarioName = "non-bot-timetable";
        opt.autoTriggers = false;

        const ExportPlan plan = planExport(route_, catalog_, sc, opt);
        QVERIFY2(plan.ok(), qPrintable(plan.errors.join("; ")));
        QVERIFY(!plan.autoEvents.isEmpty());

        bool has_route_action = false;
        for (const ScenarioEvent &event : plan.autoEvents)
        {
            for (const EventAction &action : event.actions)
            {
                has_route_action = has_route_action || action.type == ActionType::BuildTrainRoute;
            }
        }
        QVERIFY(has_route_action);

        QString err;
        QVERIFY2(writeExport(route_, catalog_, sc, plan, opt, &err), qPrintable(err));
        const QString lua = readFile(routeDir() + "/scenarios/non-bot-timetable/main.lua");
        QVERIFY(lua.contains("[rse-auto begin]"));
        QVERIFY(lua.contains("buildTrainRoute("));
        QVERIFY(lua.contains("setTimeTrigger("));
    }

    void disabledNonBotRouteEventsAreReported()
    {
        Scenario sc;
        PlacedTrain t = scenario_.trains()[0];
        t.autoMode = false;
        t.noAutoEvents = true;
        sc.add(t);

        ExportOptions opt;
        opt.info.scenarioName = "manual-routes-only";
        opt.autoTriggers = false;

        const ExportPlan plan = planExport(route_, catalog_, sc, opt);
        QVERIFY2(plan.ok(), qPrintable(plan.errors.join("; ")));
        QVERIFY(plan.autoEvents.isEmpty());
        QVERIFY(plan.warnings.join("\n").contains("автоматические события маршрутов отключены"));
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

    void warningGroups()
    {
        const ExportWarningGroups g = groupExportWarnings({
            QStringLiteral("Поезд «1»: график задан, но бот выключен - по графику ведёт "
                           "автоведения; маршруты и сигналы по графику редактор "
                           "сформирует отдельными событиями."),
            QStringLiteral("Поезд «2»: бот включён, но у крайней единицы нет автопилота - "
                           "автоведение не заработает."),
            QStringLiteral("Сцепка «3»: нет графика - после сцепки поезд будет стоять."),
            QStringLiteral("В main.lua есть правки после прошлого экспорта - при записи "
                           "они будут потеряны, предыдущая версия сохранится в main.lua.bak-1."),
            QStringLiteral("Проверка графиков: пересечение поездов «1» и «2» на траектории T."),
            QStringLiteral("Маршруты: «1», «B»: маршрут приёма не строится."),
            QStringLiteral("Событие 1 «x»: траектория «nope» не найдена.") });

        // Поезд может не поехать: оба сломанных бота, сцепка без графика,
        // ручные правки main.lua и «Маршруты: маршрут приёма не строится»
        // (ключ «не строится» приоритетнее jam-ключа «Маршруты:»)
        QCOMPARE(g.stop.size(), 5);
        // Возможные заторы: проверка графиков
        QCOMPARE(g.jam.size(), 1);
        QCOMPARE(g.info.size(), 1);
        QVERIFY(g.info.first().contains("Событие 1"));
    }

    void mainLuaProtection()
    {
        const QString scenario_dir = routeDir() + "/scenarios/guarded";

        // Первый экспорт: файла ещё нет, копия не обещается
        ExportOptions opt;
        opt.info.scenarioName = "guarded";

        ExportPlan plan = planExport(route_, catalog_, scenario_, opt);
        QVERIFY2(plan.ok(), qPrintable(plan.errors.join("; ")));
        QVERIFY(plan.mainBackupName.isEmpty());
        QVERIFY(plan.existingMainSha256.isEmpty());

        QString err;
        QVERIFY2(writeExport(route_, catalog_, scenario_, plan, opt, &err), qPrintable(err));

        const QString main_path = scenario_dir + "/main.lua";
        const QByteArray written = readFile(main_path).toUtf8();
        const QString sha = QString::fromLatin1(
            QCryptographicHash::hash(written, QCryptographicHash::Sha256).toHex());
        QVERIFY(!sha.isEmpty());

        // Пользователь отредактировал main.lua вручную: замена предупреждает
        writeFile(main_path, QStringLiteral("-- ручные правки\n") + QString::fromUtf8(written));

        opt.overwrite = true;
        opt.info.exportedMainSha256 = sha;

        plan = planExport(route_, catalog_, scenario_, opt);
        QVERIFY2(plan.ok(), qPrintable(plan.errors.join("; ")));
        QVERIFY(plan.mainEdited);
        QVERIFY(!plan.mainBackupName.isEmpty());
        QVERIFY(plan.warnings.join("\n").contains("будут потеряны"));

        // При записи предыдущая версия сохраняется в резервную копию
        QVERIFY2(writeExport(route_, catalog_, scenario_, plan, opt, &err), qPrintable(err));
        QVERIFY(readFile(scenario_dir + "/" + plan.mainBackupName).contains("ручные правки"));
        QVERIFY(readFile(main_path).contains("setTime"));

        // Экспорт того же содержимого: копия не создаётся
        const int backups_before = QDir(scenario_dir).entryList(
            QStringList(QStringLiteral("main.lua.bak-*"))).size();

        opt.info.exportedMainSha256 = QString::fromLatin1(
            QCryptographicHash::hash(readFile(main_path).toUtf8(), QCryptographicHash::Sha256).toHex());

        plan = planExport(route_, catalog_, scenario_, opt);
        QVERIFY2(plan.ok(), qPrintable(plan.errors.join("; ")));
        QVERIFY(!plan.mainEdited);
        QVERIFY2(writeExport(route_, catalog_, scenario_, plan, opt, &err), qPrintable(err));

        QCOMPARE(QDir(scenario_dir).entryList(
                     QStringList(QStringLiteral("main.lua.bak-*"))).size(), backups_before);

        // Без проекта (хеш неизвестен) - копия обещается и создаётся при замене
        writeFile(main_path, QStringLiteral("-- чужие правки\n"));

        opt.info.exportedMainSha256.clear();
        plan = planExport(route_, catalog_, scenario_, opt);
        QVERIFY2(plan.ok(), qPrintable(plan.errors.join("; ")));
        QVERIFY(!plan.mainEdited);
        QVERIFY(!plan.mainBackupName.isEmpty());
        QVERIFY(plan.warnings.join("\n").contains("неизвестно"));

        QVERIFY2(writeExport(route_, catalog_, scenario_, plan, opt, &err), qPrintable(err));

        // Последняя по имени резервная копия содержит чужие правки (при
        // нескольких экспортах в одну секунду копии получают номер -2, -3…)
        const QStringList baks = QDir(scenario_dir).entryList(
            QStringList(QStringLiteral("main.lua.bak-*")), QDir::Files, QDir::Name);
        QVERIFY(!baks.isEmpty());
        QVERIFY(readFile(scenario_dir + "/" + baks.last()).contains("чужие правки"));
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
        info.exportedMainSha256 = QStringLiteral("abc123");
        info.exportedAt = 1763900000;

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
        QCOMPARE(got.exportedMainSha256, QStringLiteral("abc123"));
        QCOMPARE(got.exportedAt, qint64(1763900000));
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

        // Закрепления трассы и ручные времена строк переживают сохранение
        Scenario sc3 = sc;
        PlacedTrain t3 = sc3.trains()[0];

        TripPin pin;
        pin.station = QStringLiteral("Станция-B");
        pin.track = QStringLiteral("T2");
        pin.trackCoord = 1500.0;
        t3.pins.push_back(pin);

        QVERIFY(!t3.timetable.stations.isEmpty());
        t3.timetable.stations[0].manualArrival = true;
        t3.timetable.stations[1].manualDeparture = true;

        sc3.replace(0, t3);

        const QString ppath = dir_.path() + "/work/pins.rrsproj";
        QVERIFY2(saveProject(ppath, info, sc3, &err), qPrintable(err));

        Scenario got_sc;
        QVERIFY2(loadProject(ppath, &got, &got_sc, &err), qPrintable(err));
        QCOMPARE(got_sc.trains()[0].pins.size(), 1);
        QCOMPARE(got_sc.trains()[0].pins[0].station, QStringLiteral("Станция-B"));
        QCOMPARE(got_sc.trains()[0].pins[0].track, QStringLiteral("T2"));
        QCOMPARE(got_sc.trains()[0].pins[0].trackCoord, 1500.0);
        QVERIFY(got_sc.trains()[0].timetable.stations[0].manualArrival);
        QVERIFY(got_sc.trains()[0].timetable.stations[1].manualDeparture);

        // Снимок проекта из байтов (основа отмены/повтора) читается тем же разбором
        const QByteArray snap = projectSnapshot(info, sc3);
        ProjectInfo snap_info;
        Scenario snap_sc;
        QVERIFY2(loadProjectData(snap, &snap_info, &snap_sc, &err), qPrintable(err));
        QCOMPARE(snap_sc.size(), 2);
        QCOMPARE(snap_sc.trains()[0].pins.size(), 1);
        QCOMPARE(snap_sc.trains()[0].pins[0].station, QStringLiteral("Станция-B"));

        // Файл не проекта
        writeFile(dir_.path() + "/x.json", "{\"a\":1}");
        QVERIFY(!loadProject(dir_.path() + "/x.json", &got, &sc, &err));
        QVERIFY(!err.isEmpty());
    }
};

QTEST_MAIN(TestExport)
#include "test-export.moc"
