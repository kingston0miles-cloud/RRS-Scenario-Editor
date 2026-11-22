#include    <QtTest>

#include    <QDir>
#include    <QFile>
#include    <QTemporaryDir>

#include    "scenario/export.h"
#include    "scenario/importer.h"
#include    "test-import-data.h"

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

    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

QString vehicleXml(double length, bool crane, bool autopilot)
{
    QString s = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<Config>\n<Vehicle>\n";
    s += QString("<EmptyMass>100000</EmptyMass>\n<PayloadMass>0</PayloadMass>\n<Length>%1</Length>\n").arg(length);

    if (crane)
    {
        s += "<BrakeCraneModule>krm394</BrakeCraneModule>\n";
    }

    s += autopilot ? "<AutopilotModule>ap</AutopilotModule>\n" : "<AutopilotModule></AutopilotModule>\n";
    s += "</Vehicle>\n</Config>\n";

    return s;
}

} // namespace

class TestImport : public QObject
{
    Q_OBJECT

private:

    QTemporaryDir dir_;
    Route route_;
    VehicleCatalog catalog_;

    QString routeDir() const
    {
        return dir_.path() + "/routes/r";
    }

private slots:

    void initTestCase()
    {
        QVERIFY(dir_.isValid());
        const QString root = dir_.path();

        writeFile(root + "/cfg/vehicles/locos/L.xml", vehicleXml(20.0, true, true));
        writeFile(root + "/cfg/trains/poezd.xml",
                  "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<Config>\n<Common>\n"
                  "<ChargingPressure>0.5</ChargingPressure>\n<Title>Поезд</Title>\n</Common>\n"
                  "<Vehicle>\n<ModuleDir>loco</ModuleDir>\n<Module>l</Module>\n"
                  "<ModuleConfigDir>locos</ModuleConfigDir>\n<ModuleConfig>L</ModuleConfig>\n"
                  "<IsOrientationForward>1</IsOrientationForward>\n<Count>1</Count>\n"
                  "<PayloadCoeff>0.0</PayloadCoeff>\n</Vehicle>\n</Config>\n");

        writeFile(routeDir() + "/topology/trajectories/T.traj", "0 0 0 0\n0 400 0 400\n");
        writeFile(routeDir() + "/topology/trajectories/U.traj", "0 400 0 400\n0 800 0 800\n");
        writeFile(routeDir() + "/topology/topology.xml",
                  "<Config><Switch><Name>s</Name><bwdPlusTraj>T</bwdPlusTraj>"
                  "<fwdPlusTraj>U</fwdPlusTraj></Switch></Config>\n");

        QVERIFY2(route_.load(routeDir()), qPrintable(route_.errorString()));
        QVERIFY2(catalog_.load(root), qPrintable(catalog_.errorString()));
    }

    void recognizesTrainsAndEvents()
    {
        const QString lua = QString::fromUtf8(SAMPLE_LUA);

        const ImportResult r = importLua(route_, catalog_, lua);
        QVERIFY2(r.ok, qPrintable(r.error));

        QCOMPARE(r.info.startTime, 17 * 3600 + 30 * 60);

        // Поезд t1 распознан, t2 (нет состава) сохранён как код
        QCOMPARE(r.scenario.size(), 1);
        const PlacedTrain &t = r.scenario.trains()[0];
        QCOMPARE(t.name, QString("34 \"быстрый\""));
        QCOMPARE(t.luaVar, QString("t1"));
        QCOMPARE(t.sourceTrain, QString("poezd"));
        QCOMPARE(t.dir, -1);
        QVERIFY(t.autoMode);
        QCOMPARE(t.coord, 100.0);
        QVERIFY(!r.warnings.isEmpty());

        // События: время, занятие поездом (функция), задержка, авто-пропуск; custom - не распознан
        QCOMPARE(r.info.events.size(), 4);

        const ScenarioEvent &e0 = r.info.events[0];
        QCOMPARE(e0.trigger, TriggerType::AtTime);
        QCOMPARE(e0.time, 17 * 3600 + 31 * 60);
        QCOMPARE(e0.actions.size(), 1);
        QCOMPARE(e0.actions[0].a, QString("T"));     // t1.traj
        QCOMPARE(e0.actions[0].b, QString("U"));     // константа
        QCOMPARE(e0.title, QString("Отправление"));

        const ScenarioEvent &e1 = r.info.events[1];
        QCOMPARE(e1.trigger, TriggerType::TrajBusy);
        QCOMPARE(e1.train, QString("34 \"быстрый\""));
        QCOMPARE(e1.actions.size(), 3);
        QCOMPARE(e1.actions[1].type, ActionType::OpenSignal);
        QCOMPARE(e1.actions[1].dir, 0);

        const ScenarioEvent &e2 = r.info.events[2];
        QCOMPARE(e2.trigger, TriggerType::TrajFree);
        QCOMPARE(e2.delaySec, 80);
        QCOMPARE(e2.actions[0].type, ActionType::ReverseTrain);

        QCOMPARE(r.info.events[3].trigger, TriggerType::AutoApproach);

        // Остаток: поезд t2, функция custom, setTrigger и setOnTrajBusyTrigger(custom)
        const QString pre = r.info.opaquePre.join("\n");
        const QString post = r.info.opaquePost.join("\n");
        QVERIFY(pre.contains("t2.config = \"no-such-train\""));
        QVERIFY(pre.contains("function custom()"));
        QVERIFY(!pre.contains("function dep()"));      // поглощена событием
        QVERIFY(!pre.contains("function wrap()"));
        QVERIFY(!pre.contains("unused"));              // константы без ссылок убраны
        QVERIFY(!pre.contains("local stU"));
        QVERIFY(post.contains("setTrigger(custom)"));
        QVERIFY(post.contains("setOnTrajBusyTrigger(\"U\", custom)"));

        // Круг: экспорт -> импорт даёт то же
        ExportOptions opt;
        opt.info = r.info;
        opt.info.scenarioName = "rt";
        const ExportPlan plan = planExport(route_, catalog_, r.scenario, opt);
        QVERIFY2(plan.ok(), qPrintable(plan.errors.join("; ")));

        const QString out = luaMain(r.scenario, plan, opt.info);
        QVERIFY(out.contains("t1.name = \"34 \\\"быстрый\\\"\""));
        QVERIFY(out.contains("setTrain(t1)"));
        QVERIFY(out.contains("function custom()"));

        const ImportResult r2 = importLua(route_, catalog_, out);
        QVERIFY2(r2.ok, qPrintable(r2.error));
        QCOMPARE(r2.scenario.size(), 1);
        QCOMPARE(r2.scenario.trains()[0].name, t.name);
        QCOMPARE(r2.info.events.size(), 4);
        QCOMPARE(r2.info.events[1].actions.size(), 3);
        QCOMPARE(r2.info.events[2].delaySec, 80);
        QCOMPARE(r2.info.startTime, r.info.startTime);
        QCOMPARE(r2.info.opaquePost.size(), r.info.opaquePost.size());
    }

    void badSourceIsRejected()
    {
        QVERIFY(!importLua(route_, catalog_, "x = \"abc\n").ok);
        QVERIFY(!importLua(route_, catalog_, "--[[ не закрыт").ok);
        QVERIFY(importLua(route_, catalog_, "").ok);
    }

    void importsDirectoryWithTimetable()
    {
        const QString sc = routeDir() + "/scenarios/old";

        writeFile(sc + "/main.lua",
                  "setTime(\"11:59\")\n"
                  "train1 = TrainData.new()\ntrain1.name = \"134\"\ntrain1.config = \"poezd\"\n"
                  "train1.traj = \"T\"\ntrain1.coord = 100.0\ntrain1.dir = 1\ntrain1.auto = true\n"
                  "setTrain(train1)\n");
        writeFile(sc + "/timetable/134.xml",
                  "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<Config>\n"
                  "<Station><Name>A</Name><DepartureTime>12:00</DepartureTime><TargetTraj>T</TargetTraj>"
                  "<TargetCoord>100.0</TargetCoord><RemovalTraj>U</RemovalTraj></Station>\n"
                  "<Station><Name>B</Name><ArrivalTime>12:20</ArrivalTime><DepartureTime>12:25</DepartureTime>"
                  "<TargetTraj>U</TargetTraj><TargetCoord>300.0</TargetCoord><ApproachTraj>T</ApproachTraj>"
                  "<IsRightPlatform>1</IsRightPlatform></Station>\n"
                  "<Station><Name>C</Name><ArrivalTime>00:05</ArrivalTime><TargetTraj>U</TargetTraj>"
                  "<TargetCoord>380.0</TargetCoord></Station>\n"
                  "</Config>\n");
        writeFile(sc + "/README.md", "## Старый\n\nОписание старого сценария.\n");

        const ImportResult r = importScenario(route_, catalog_, sc);
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(r.info.scenarioName, QString("old"));
        QCOMPARE(r.scenario.size(), 1);

        const Timetable &tt = r.scenario.trains()[0].timetable;
        QVERIFY(tt.valid);
        QCOMPARE(tt.stations.size(), 3);
        QCOMPARE(tt.startTime, 12 * 3600);
        QCOMPARE(tt.stations[1].arrival, 12 * 3600 + 20 * 60);
        QCOMPARE(tt.stations[1].departure, 12 * 3600 + 25 * 60);
        QCOMPARE(tt.stations[1].rightPlatform, 1);
        QCOMPARE(tt.stations[0].removalTrajectory, QString("U"));
        // последняя станция после полуночи
        QCOMPARE(tt.stations[2].arrival, 86400 + 5 * 60);
        QCOMPARE(tt.endTrajectory, QString("U"));
        QCOMPARE(tt.endCoord, 380.0);
        QVERIFY(r.info.description.contains("Описание старого"));

        // Повторный экспорт сохраняет поля расписания
        ExportOptions opt;
        opt.info = r.info;
        opt.overwrite = true;
        const ExportPlan plan = planExport(route_, catalog_, r.scenario, opt);
        QVERIFY2(plan.ok(), qPrintable(plan.errors.join("; ")));
        QString err;
        QVERIFY2(writeExport(route_, catalog_, r.scenario, plan, opt, &err), qPrintable(err));

        const QString xml = readFile(sc + "/timetable/134.xml");
        QVERIFY(xml.contains("<RemovalTraj>U</RemovalTraj>"));
        QVERIFY(xml.contains("<ApproachTraj>T</ApproachTraj>"));
        QVERIFY(xml.contains("<IsRightPlatform>1</IsRightPlatform>"));

        const QString lua = readFile(sc + "/main.lua");
        QVERIFY(lua.contains("train1.name = \"134\""));

        // Проект хранит непонятный Lua и имена переменных
        ProjectInfo info = r.info;
        info.opaquePre = { "function f() end" };
        info.opaquePost = { "f()" };
        const QString proj = dir_.path() + "/i.rrsproj";
        QVERIFY(saveProject(proj, info, r.scenario, &err));
        ProjectInfo got;
        Scenario s2;
        QVERIFY(loadProject(proj, &got, &s2, &err));
        QCOMPARE(got.opaquePre, info.opaquePre);
        QCOMPARE(got.opaquePost, info.opaquePost);
        QCOMPARE(s2.trains()[0].luaVar, QString("train1"));
        QCOMPARE(s2.trains()[0].timetable.stations[1].rightPlatform, 1);
    }

    void realScenarios()
    {
        const QString route_dir = qEnvironmentVariable("RRS_ROUTE_DIR");

        if (route_dir.isEmpty())
        {
            QSKIP("RRS_ROUTE_DIR не задан");
        }

        Route r;
        QVERIFY(r.load(route_dir));

        VehicleCatalog cat;
        QVERIFY(cat.load(VehicleCatalog::gameRootForRoute(route_dir)));

        const QDir sdir(route_dir + "/scenarios");
        int checked = 0;

        for (const QFileInfo &fi : sdir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot))
        {
            if (!QFileInfo::exists(fi.absoluteFilePath() + "/main.lua"))
            {
                continue;
            }

            const ImportResult res = importScenario(r, cat, fi.absoluteFilePath());
            QVERIFY2(res.ok, qPrintable(fi.fileName() + ": " + res.error));
            QVERIFY2(res.scenario.size() > 0, qPrintable(fi.fileName()));

            // Круг: Lua после экспорта разбирается так же
            ExportOptions opt;
            opt.info = res.info;
            opt.info.scenarioName = "roundtrip";
            const ExportPlan plan = planExport(r, cat, res.scenario, opt);

            // Расписания в реальных сценариях могут вести поезда и пересекаться - нас
            // интересует только текст
            const QString lua = luaMain(res.scenario, plan, opt.info);
            const ImportResult back = importLua(r, cat, lua);
            QVERIFY2(back.ok, qPrintable(fi.fileName() + ": " + back.error));
            QCOMPARE(back.scenario.size(), res.scenario.size());
            QCOMPARE(back.info.events.size(), res.info.events.size());
            QCOMPARE(back.info.opaquePre.size(), res.info.opaquePre.size());
            QCOMPARE(back.info.opaquePost.size(), res.info.opaquePost.size());
            if (res.info.startTime >= 0)
            {
                QCOMPARE(back.info.startTime, res.info.startTime);
            }

            for (int i = 0; i < res.info.events.size(); ++i)
            {
                QCOMPARE(triggerText(back.info.events[i]), triggerText(res.info.events[i]));
                QCOMPARE(back.info.events[i].actions.size(), res.info.events[i].actions.size());
            }

            ++checked;
        }

        QVERIFY(checked >= 5);
    }
};

QTEST_MAIN(TestImport)
#include "test-import.moc"
