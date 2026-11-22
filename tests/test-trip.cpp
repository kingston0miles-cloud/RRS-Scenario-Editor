#include    <QtTest>

#include    <QDir>
#include    <QFile>
#include    <QTemporaryDir>

#include    "scenario/trip.h"

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

//------------------------------------------------------------------------------
// Прямая T1 длиной 8 км вдоль Y (пикетаж = y + 1000), ограничение 60 км/ч на
// 3000...4000 м (пикет 4000...5000); станции A (0), B (4000), C (8000) и боковая D
// на пути Z
//------------------------------------------------------------------------------
void buildRoute(const QString &root)
{
    QString traj;

    for (int y = 0; y <= 8000; y += 500)
    {
        traj += QStringLiteral("0 %1 0 %2\n").arg(y).arg(y + 1000);
    }

    writeFile(root + "/topology/trajectories/T1.traj", traj);
    writeFile(root + "/topology/trajectories/Z.traj", "0 0 0 100\n0 -300 0 -200\n");
    writeFile(root + "/topology/topology.xml", "<?xml version=\"1.0\"?>\n<Config/>\n");
    writeFile(root + "/topology/stations.conf",
              "Станция-A\t0\t0\t0\n"
              "Станция-B\t0\t4000\t0\n"
              "Станция-C\t0\t8000\t0\n");
    writeFile(root + "/topology/trajectory-speedmap/60.xml",
              "<?xml version=\"1.0\"?>\n<Config>\n"
              "<Trajectory><Name>T1</Name></Trajectory>\n"
              "<SpeedMap><SpeedLimit>60 4000 5000</SpeedLimit></SpeedMap>\n"
              "</Config>\n");
}

PlacedTrain makeTrain(const QString &name, const QString &traj, double coord, int dir)
{
    PlacedTrain t;
    t.name = name;
    t.title = name;
    t.trajectory = traj;
    t.coord = coord;
    t.dir = dir;
    t.length = 200.0;
    t.mass = 1.0e6;

    return t;
}

} // namespace

class TestTrip : public QObject
{
    Q_OBJECT

private:

    QTemporaryDir dir_;
    Route route_;
    VehicleCatalog catalog_;

    TripPlan plan(const Scenario &sc, double end = 8000.0)
    {
        TripPlan p = buildTripPlan(route_, catalog_, sc, 0, "T1", end);
        // Состав без каталога: зададим динамику вручную
        p.dynamics.mass = 1.0e6;
        p.dynamics.length = 200.0;
        p.dynamics.a0 = 5000.0;
        p.dynamics.a1 = 100.0;
        p.dynamics.a2 = 8.0;
        p.dynamics.traction.powerKw = 3000.0;
        p.dynamics.traction.startForceKn = 300.0;
        p.dynamics.traction.vMaxKmh = 100.0;
        p.dynamics.traction.adhesiveMass = 0.0;
        return p;
    }

private slots:

    void initTestCase();
    void planStationsAndLimits();
    void runRespectsLimitAndPhysics();
    void stopsAndMargins();
    void conflictsWithOtherTrains();
    void noTractionWarning();
};

void TestTrip::initTestCase()
{
    QVERIFY(dir_.isValid());
    buildRoute(dir_.path());
    QVERIFY2(route_.load(dir_.path()), qPrintable(route_.errorString()));
}

void TestTrip::planStationsAndLimits()
{
    Scenario sc;
    sc.add(makeTrain("P1", "T1", 0.0, 1));

    const TripPlan p = plan(sc);
    QVERIFY2(p.ok, qPrintable(p.error));
    QVERIFY(qAbs(p.length - 8000.0) < 1.0);
    QCOMPARE(p.stations.size(), 3);
    QCOMPARE(p.stations[1].name, QString("Станция-B"));
    QVERIFY(qAbs(p.stations[1].s - 4000.0) < 25.0);

    // 60 км/ч с учётом хвоста: действует, пока хвост (200 м) не выйдет
    const int i = static_cast<int>(3500.0 / p.profile.step);
    QVERIFY(qAbs(p.profile.limit[i] * 3.6 - 60.0) < 0.1);
    const int j = static_cast<int>(2900.0 / p.profile.step);
    QVERIFY(p.profile.limit[j] * 3.6 > 250.0);
    const int k = static_cast<int>(4100.0 / p.profile.step);
    QVERIFY(qAbs(p.profile.limit[k] * 3.6 - 60.0) < 0.1);
    const int m = static_cast<int>(4300.0 / p.profile.step);
    QVERIFY(p.profile.limit[m] * 3.6 > 250.0);
}

void TestTrip::runRespectsLimitAndPhysics()
{
    Scenario sc;
    sc.add(makeTrain("P1", "T1", 0.0, 1));

    TripPlan p = plan(sc);
    TripOptions o;
    o.startTime = 10 * 3600;

    const TripResult r = computeTrip(p, o, route_, sc, 0);
    QVERIFY2(r.ok, qPrintable(r.error));
    QCOMPARE(r.rows.size(), 2);

    // Быстрее, чем на 100 км/ч, не доехать; медленнее двадцати - тоже нет
    QVERIFY(r.runTime > 8000.0 / (100.0 / 3.6));
    QVERIFY(r.runTime < 8000.0 / (20.0 / 3.6));

    // Без ограничения быстрее
    Route plain;
    QTemporaryDir d2;
    buildRoute(d2.path());
    QFile::remove(d2.path() + "/topology/trajectory-speedmap/60.xml");
    QVERIFY(plain.load(d2.path()));
    TripPlan p2 = buildTripPlan(plain, catalog_, sc, 0, "T1", 8000.0);
    p2.dynamics = p.dynamics;
    const TripResult r2 = computeTrip(p2, o, plain, sc, 0);
    QVERIFY(r2.ok);
    QVERIFY(r2.runTime < r.runTime);

    // Скорость в зоне ограничения не выше 60 км/ч
    const RunResult run = simulateRun(p.dynamics, p.profile, {});
    for (int i = static_cast<int>(3200.0 / 5.0); i < static_cast<int>(4150.0 / 5.0); ++i)
    {
        QVERIFY2(run.speed[i] * 3.6 <= 60.5, qPrintable(QString::number(i)));
    }

    // Торможение до ограничения: на подходе к 3000 м скорость снижается
    QVERIFY(run.speed[static_cast<int>(2000.0 / 5.0)] > run.speed[static_cast<int>(2990.0 / 5.0)]);
    QVERIFY(run.speed.last() < 3.0);
}

void TestTrip::stopsAndMargins()
{
    Scenario sc;
    sc.add(makeTrain("P1", "T1", 0.0, 1));

    TripPlan p = plan(sc);

    TripOptions o;
    o.startTime = 10 * 3600;

    StopSpec b;
    b.station = 1;
    b.mode = StopMode::Stop;
    b.dwellSec = 100;
    o.stops.push_back(b);

    const TripResult none = computeTrip(p, o, route_, sc, 0);
    QVERIFY2(none.ok, qPrintable(none.error));
    QCOMPARE(none.rows.size(), 3);
    QCOMPARE(none.rows[1].name, QString("Станция-B"));

    // Стоянка округляется до минуты вверх
    QCOMPARE(none.rows[1].departure - none.rows[1].arrival, 120);
    QVERIFY(none.rows[2].arrival > none.rows[1].departure);
    QCOMPARE(none.rows[1].arrival % 60, 0);
    QVERIFY(none.rows[1].speedKmh < 5.0);

    // Запас на весь путь
    o.totalExtraMin = 6.0;
    const TripResult with = computeTrip(p, o, route_, sc, 0);
    const int delta = with.rows.last().arrival - none.rows.last().arrival;
    QVERIFY2(delta >= 6 * 60 - 60 && delta <= 6 * 60 + 60, qPrintable(QString::number(delta)));
    QVERIFY(with.rows.last().reserve > 0);

    // Личный запас на перегон до B: задерживает B и всё после него
    o.totalExtraMin = 0.0;
    o.stops[0].extraMin = 3.0;
    const TripResult own = computeTrip(p, o, route_, sc, 0);
    const int d1 = own.rows[1].arrival - none.rows[1].arrival;
    const int d2 = own.rows[2].arrival - none.rows[2].arrival;
    QVERIFY(d1 >= 3 * 60 - 60 && d1 <= 3 * 60 + 60);
    QVERIFY(d2 >= d1);

    // Проезд без остановки: прибытие = отправление
    o.stops[0].mode = StopMode::Pass;
    o.stops[0].extraMin = 0.0;
    const TripResult pass = computeTrip(p, o, route_, sc, 0);
    QCOMPARE(pass.rows[1].arrival, pass.rows[1].departure);
    QVERIFY(pass.rows[1].speedKmh > 20.0);
    QVERIFY(pass.rows.last().arrival < none.rows.last().arrival);

    // Расписание для сценария
    QVERIFY(pass.timetable.valid);
    QCOMPARE(pass.timetable.stations.size(), 3);
    QCOMPARE(pass.timetable.stations[0].arrival, o.startTime);

    // Время по умолчанию с запасом
    QVERIFY(defaultReserveMin(1000.0) >= 2.0);
    QCOMPARE(formatClock(3661), QString("01:01:01"));
}

void TestTrip::conflictsWithOtherTrains()
{
    Scenario sc;
    sc.add(makeTrain("P1", "T1", 0.0, 1));

    // Стоящий поезд на пути
    sc.add(makeTrain("Stand", "T1", 6000.0, 1));

    TripPlan p = plan(sc);
    QVERIFY(!p.warnings.isEmpty());
    QVERIFY(p.warnings.join("\n").contains("Stand"));

    sc.remove(1);

    // Встречный поезд по расписанию
    PlacedTrain other = makeTrain("Opp", "T1", 8000.0, -1);
    TripPlan p0 = plan(sc);
    TripOptions o;
    o.startTime = 10 * 3600;
    const TripResult mine = computeTrip(p0, o, route_, sc, 0);
    QVERIFY(mine.ok);
    QVERIFY(mine.warnings.isEmpty());

    // Тот же путь в обратную сторону, в то же время
    other.timetable.valid = true;
    for (const TimetableSample &s : mine.timetable.samples)
    {
        TimetableSample t = s;
        t.dir = -1;
        other.timetable.samples.push_back(t);
    }
    sc.add(other);

    const TripResult clash = computeTrip(p0, o, route_, sc, 0);
    QVERIFY(clash.ok);
    QVERIFY(clash.warnings.join("\n").contains("Opp"));

    // Через час - конфликта нет
    sc.remove(1);
    for (TimetableSample &s : other.timetable.samples)
    {
        s.time += 3600;
    }
    sc.add(other);
    const TripResult later = computeTrip(p0, o, route_, sc, 0);
    QVERIFY(later.warnings.isEmpty());
}

void TestTrip::noTractionWarning()
{
    Scenario sc;
    sc.add(makeTrain("P1", "T1", 0.0, 1));

    TripPlan p = plan(sc);
    // Слабая тяга: не трогается
    p.dynamics.traction.startForceKn = 1.0;
    p.dynamics.traction.powerKw = 10.0;

    TripOptions o;
    const TripResult r = computeTrip(p, o, route_, sc, 0);
    QVERIFY(r.ok);
    QVERIFY(!r.warnings.isEmpty());
}

QTEST_MAIN(TestTrip)
#include "test-trip.moc"
