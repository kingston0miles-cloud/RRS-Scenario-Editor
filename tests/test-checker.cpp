#include    <QtTest>

#include    <QDir>
#include    <QFile>
#include    <QTemporaryDir>

#include    "scenario/checker.h"
#include    "scenario/formation.h"

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

void buildRoute(const QString &root)
{
    QString traj;

    for (int y = 0; y <= 8000; y += 500)
    {
        traj += QStringLiteral("0 %1 0 %2\n").arg(y).arg(y + 1000);
    }

    writeFile(root + "/topology/trajectories/T1.traj", traj);
    writeFile(root + "/topology/topology.xml", "<?xml version=\"1.0\"?>\n<Config/>\n");
}

constexpr int T0 = 12 * 3600;

PlacedTrain make(const QString &name, double coord, int dir)
{
    PlacedTrain t;
    t.name = name;
    t.title = name;
    t.trajectory = "T1";
    t.coord = coord;
    t.dir = dir;
    t.length = 200.0;
    t.mass = 1.0e6;
    t.autoMode = true;
    return t;
}

/// Расписание: движение с c0 на c1 за [from, to] секунд, точки раз в 30 с
void drive(PlacedTrain &t, int from, int to, double c0, double c1)
{
    Timetable tt;
    tt.valid = true;
    tt.startTime = from;
    tt.endTrajectory = "T1";
    tt.endCoord = c1;

    const int dir = c1 >= c0 ? 1 : -1;

    for (int s = from; s <= to; s += 30)
    {
        TimetableSample sm;
        sm.time = s;
        sm.trajectory = "T1";
        sm.coord = c0 + (c1 - c0) * double(s - from) / double(to - from);
        sm.dir = dir;
        tt.samples.push_back(sm);
    }

    TimetableStation a;
    a.name = "A";
    a.departure = from;
    a.arrival = from;
    a.targetTrajectory = "T1";
    a.targetCoord = c0;

    TimetableStation b;
    b.name = "B";
    b.arrival = to;
    b.departure = to;
    b.targetTrajectory = "T1";
    b.targetCoord = c1;

    tt.stations << a << b;
    t.timetable = tt;
    t.coord = c0;
    t.dir = dir;
}

bool has(const QVector<ScheduleIssue> &v, const QString &kind)
{
    for (const ScheduleIssue &i : v)
    {
        if (i.kind == kind)
        {
            return true;
        }
    }

    return false;
}

} // namespace

class TestChecker : public QObject
{
    Q_OBJECT

private slots:

    void init()
    {
        QVERIFY(dir_.isValid());
        buildRoute(dir_.path());
        route_.load(dir_.path());
        QVERIFY(route_.isLoaded());
        opt_.startTime = T0 - 60;
    }

    void cleanRunHasNoErrors()
    {
        Scenario sc;
        PlacedTrain a = make("A", 500, 1);
        drive(a, T0, T0 + 600, 500, 3500);
        sc.add(a);

        PlacedTrain b = make("B", 7000, -1);
        drive(b, T0 + 1200, T0 + 1800, 7000, 4500);
        sc.add(b);

        const auto issues = checkSchedules(route_, sc, opt_);

        for (const ScheduleIssue &i : issues)
        {
            QVERIFY2(i.severity == IssueSeverity::Info, qPrintable(i.text));
        }
    }

    void followingTooCloseGivesHeadway()
    {
        Scenario sc;
        PlacedTrain a = make("A", 1500, 1);
        drive(a, T0, T0 + 600, 1500, 4500);
        sc.add(a);

        PlacedTrain b = make("B", 500, 1);
        drive(b, T0, T0 + 600, 500, 3500);     // на 1000 м позади, но с той же скоростью
        sc.add(b);

        const auto issues = checkSchedules(route_, sc, opt_);
        QVERIFY(!has(issues, "collision"));
        QVERIFY(!has(issues, "headway"));

        Scenario sc2;
        sc2.add(a);
        PlacedTrain c = make("C", 1300, 1);
        drive(c, T0, T0 + 600, 1300, 4300);    // 200 м позади (длина 200): впритык
        sc2.add(c);

        QVERIFY(has(checkSchedules(route_, sc2, opt_), "headway"));
    }

    void headOnIsError()
    {
        Scenario sc;
        PlacedTrain a = make("A", 500, 1);
        drive(a, T0, T0 + 600, 500, 5000);
        sc.add(a);

        PlacedTrain b = make("B", 6000, -1);
        drive(b, T0, T0 + 600, 6000, 1500);
        sc.add(b);

        const auto issues = checkSchedules(route_, sc, opt_);
        QVERIFY(has(issues, "head-on"));
        QCOMPARE(issues.first().severity, IssueSeverity::Error);
    }

    void arrivingOnStandingTrainIsCollision()
    {
        Scenario sc;
        PlacedTrain a = make("A", 500, 1);
        drive(a, T0, T0 + 600, 500, 5000);
        sc.add(a);

        sc.add(make("Stand", 5100, 1));       // стоит там, где A остановится

        QVERIFY(has(checkSchedules(route_, sc, opt_), "collision"));
    }

    void approachingStandingTrainCouples()
    {
        Scenario sc;
        PlacedTrain a = make("A", 500, 1);
        drive(a, T0, T0 + 600, 500, 4000);
        sc.add(a);

        sc.add(make("Stand", 4410, 1));        // хвост на 4210: до головы A 210 м... дальше зоны
        QVERIFY(!has(checkSchedules(route_, sc, opt_), "couple"));

        Scenario sc2;
        sc2.add(a);
        sc2.add(make("Stand", 4300, 1));       // хвост на 4100: до головы A 100 м
        QVERIFY(!has(checkSchedules(route_, sc2, opt_), "couple"));

        Scenario sc3;
        sc3.add(a);
        sc3.add(make("Stand", 4205, 1));       // хвост на 4005, голова A на 4000
        QVERIFY(has(checkSchedules(route_, sc3, opt_), "couple"));
    }

    void timetableMistakes()
    {
        Scenario sc;
        PlacedTrain a = make("A", 500, 1);
        drive(a, T0 - 3600, T0 - 3000, 500, 3000);   // раньше старта
        a.autoMode = false;                          // и без бота
        sc.add(a);

        const auto issues = checkSchedules(route_, sc, opt_);
        QVERIFY(has(issues, "before-start"));
        QVERIFY(has(issues, "no-bot"));

        Scenario sc2;
        PlacedTrain b = make("B", 500, 1);
        drive(b, T0, T0 + 600, 500, 3000);
        b.timetable.stations[1].arrival = T0 - 10;   // прибытие раньше отправления
        sc2.add(b);
        QVERIFY(has(checkSchedules(route_, sc2, opt_), "order"));

        Scenario sc3;
        PlacedTrain c = make("C", 500, 1);
        drive(c, T0, T0 + 600, 500, 3000);
        c.timetable.samples.clear();
        sc3.add(c);
        QVERIFY(has(checkSchedules(route_, sc3, opt_), "no-samples"));
    }

    void formationOrderAndTiming()
    {
        Scenario sc;

        // вагоны стоят впереди (4300, голова смотрит вперёд -> хвост 4100); локомотив приходит
        sc.add(make("Wagons", 4300, 1));
        sc.add(make("Loco", 500, 1));

        PlacedTrain loco = sc.trains()[1];
        loco.length = 20.0;
        drive(loco, T0, T0 + 600, 500, 4095);
        sc.replace(1, loco);

        PlacedTrain cont = make("Out", 0, 1);
        cont.formedFrom = "Loco";
        cont.formedWagons = "Wagons";
        cont.formDelaySec = 120;
        sc.add(cont);

        const FormationPlan plan = planFormation(route_, sc, sc.trains()[2]);
        QVERIFY2(plan.ok, qPrintable(plan.error));
        QVERIFY(plan.couples);
        QCOMPARE(plan.arrivalTime, T0 + 600);
        QCOMPARE(plan.readyTime, T0 + 720);
        QCOMPARE(plan.formed.dir, -1);
        QCOMPARE(plan.formed.coord, 4075.0);
        QCOMPARE(plan.formed.length, 220.0);

        // вагоны стоят до локомотива в списке: после сортировки локомотив идёт первым
        const QVector<int> order = exportOrder(sc);
        QCOMPARE(order.size(), 2);
        QCOMPARE(sc.trains()[order[0]].name, QString("Loco"));

        // без графика у продолжения - предупреждение, но не ошибка пересечения
        auto issues = checkSchedules(route_, sc, opt_);
        QVERIFY(!has(issues, "collision"));
        QVERIFY(!has(issues, "couple"));

        // отправление раньше сцепки - ошибка
        PlacedTrain out = sc.trains()[2];
        drive(out, T0 + 650, T0 + 1200, 4080, 500);
        sc.replace(2, out);
        issues = checkSchedules(route_, sc, opt_);
        QVERIFY(has(issues, "formation"));

        // вовремя - формирование чистое, продолжение не сталкивается с родителями
        out = sc.trains()[2];
        drive(out, T0 + 800, T0 + 1400, 4080, 500);
        sc.replace(2, out);
        issues = checkSchedules(route_, sc, opt_);

        for (const ScheduleIssue &i : issues)
        {
            QVERIFY2(i.severity == IssueSeverity::Info || i.kind == "no-bot",
                     qPrintable(i.kind + ": " + i.text));
        }
    }

private:

    QTemporaryDir dir_;
    Route route_;
    CheckOptions opt_;
};

QTEST_MAIN(TestChecker)
#include "test-checker.moc"
