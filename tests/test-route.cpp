#include    <QtTest>

#include    <QDir>
#include    <QFile>
#include    <QTemporaryDir>

#include    "route/route.h"

using namespace rse;

namespace
{

//------------------------------------------------------------------------------
// Записать текстовый файл (UTF-8), создав каталоги
//------------------------------------------------------------------------------
void writeFile(const QString &path, const QString &content)
{
    QDir().mkpath(QFileInfo(path).absolutePath());

    QFile f(path);
    QVERIFY2(f.open(QIODevice::WriteOnly), qPrintable(path));
    f.write(content.toUtf8());
}

//------------------------------------------------------------------------------
// Небольшой маршрут с известными ответами
//------------------------------------------------------------------------------
void buildSyntheticRoute(const QString &root)
{
    // a: (0,0) -> (100,0) -> (100,100), длина 200 м
    writeFile(root + "/topology/trajectories/a.traj",
              "0 0 0 0\r\n100 0 0 100\r\n100 100 0 200\r\n");

    // b: (100,100) -> (100,200), длина 100 м
    writeFile(root + "/topology/trajectories/b.traj",
              "100 100 0 0\n100 200 0 100\n");

    // Вторая точка отстоит от первой на 5 мм - должна слиться
    writeFile(root + "/topology/trajectories/dup.traj",
              "0 50 0 0\n0 50.005 0 0\n0 150 0 100\n");

    // Подъём 3 м на 4 м в плане: длина в 3D равна 5 м
    writeFile(root + "/topology/trajectories/slope.traj",
              "0 300 0 0\n4 300 3 5\n");

    writeFile(root + "/topology/topology.xml",
              "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
              "<Config>\n"
              "  <Switch>\n"
              "    <Name>s1</Name>\n"
              "    <bwdPlusTraj>a</bwdPlusTraj>\n"
              "    <fwdPlusTraj>b</fwdPlusTraj>\n"
              "    <SignalLiterFwd>Ч</SignalLiterFwd>\n"
              "  </Switch>\n"
              "  <Switch>\n"
              "    <Name>s_bad</Name>\n"
              "    <bwdPlusTraj>a</bwdPlusTraj>\n"
              "    <fwdPlusTraj>ghost</fwdPlusTraj>\n"
              "  </Switch>\n"
              "  <Switch>\n"
              "    <Name>s_rev</Name>\n"
              "    <bwdPlusTraj>a</bwdPlusTraj>\n"
              "    <fwdMinusTrajReversed>b</fwdMinusTrajReversed>\n"
              "  </Switch>\n"
              "</Config>\n");

    writeFile(root + "/topology/stations.conf",
              "Станция-1\t10\t20\t0\r\n");

    writeFile(root + "/topology/waypoints.conf",
              "Станция-1 (Путь_1)\ta\t1\t30\t0\n");

    writeFile(root + "/description.xml",
              "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
              "<Config><Route><Title>Тестовый маршрут</Title></Route></Config>\n");
}

} // namespace

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
class TestRoute : public QObject
{
    Q_OBJECT

private slots:

    void rejectsMissingDirectory();
    void rejectsDirectoryWithoutTrajectories();
    void loadsSyntheticRoute();
    void mergesNearlyEqualPoints();
    void countsHeightInLength();
    void readsTopologyAndStations();
    void switchPositions();
    void warnsAboutUnknownTrajectory();
    void pointAtInterpolates();
    void nearestProjectsToTrajectory();
    void loadsRealRoute();
};

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void TestRoute::rejectsMissingDirectory()
{
    Route r;

    QVERIFY(!r.load("/no/such/dir/for/route"));
    QVERIFY(!r.errorString().isEmpty());
    QVERIFY(!r.isLoaded());
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void TestRoute::rejectsDirectoryWithoutTrajectories()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    Route r;

    QVERIFY(!r.load(tmp.path()));
    QVERIFY(r.errorString().contains("trajectories"));
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void TestRoute::loadsSyntheticRoute()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    buildSyntheticRoute(tmp.path());

    Route r;
    QVERIFY2(r.load(tmp.path()), qPrintable(r.errorString()));

    QCOMPARE(r.trajectories().size(), 4);
    QCOMPARE(r.title(), QString("Тестовый маршрут"));

    const TrajectoryData *a = r.find("a");
    QVERIFY(a != nullptr);
    QCOMPARE(a->points.size(), 3);
    QVERIFY(qAbs(a->length - 200.0) < 1e-9);

    // Габарит всего маршрута: x 0..100, y 0..300
    QVERIFY(qAbs(r.bounds().left() - 0.0) < 1e-9);
    QVERIFY(qAbs(r.bounds().right() - 100.0) < 1e-9);
    QVERIFY(qAbs(r.bounds().top() - 0.0) < 1e-9);
    QVERIFY(qAbs(r.bounds().bottom() - 300.0) < 1e-9);
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void TestRoute::mergesNearlyEqualPoints()
{
    QTemporaryDir tmp;
    buildSyntheticRoute(tmp.path());

    Route r;
    QVERIFY(r.load(tmp.path()));

    const TrajectoryData *d = r.find("dup");
    QVERIFY(d != nullptr);

    // Три строки, но две точки: отрезок в 5 мм не создаётся
    QCOMPARE(d->points.size(), 2);
    QVERIFY(qAbs(d->length - 99.995) < 1e-6);
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void TestRoute::countsHeightInLength()
{
    QTemporaryDir tmp;
    buildSyntheticRoute(tmp.path());

    Route r;
    QVERIFY(r.load(tmp.path()));

    const TrajectoryData *s = r.find("slope");
    QVERIFY(s != nullptr);
    QVERIFY(qAbs(s->length - 5.0) < 1e-9);
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void TestRoute::readsTopologyAndStations()
{
    QTemporaryDir tmp;
    buildSyntheticRoute(tmp.path());

    Route r;
    QVERIFY(r.load(tmp.path()));

    QCOMPARE(r.switches().size(), 3);
    QCOMPARE(r.switches().first().name, QString("s1"));
    QCOMPARE(r.switches().first().bwdPlus, QString("a"));
    QCOMPARE(r.switches().first().fwdPlus, QString("b"));
    QCOMPARE(r.switches().first().signalFwd, QString("Ч"));
    QCOMPARE(r.switches().last().fwdMinusReversed, QString("b"));

    QCOMPARE(r.stations().size(), 1);
    QCOMPARE(r.stations().first().name, QString("Станция-1"));
    QCOMPARE(r.stations().first().pos, QPointF(10.0, 20.0));

    QCOMPARE(r.waypoints().size(), 1);
    QCOMPARE(r.waypoints().first().trajectory, QString("a"));
    QCOMPARE(r.waypoints().first().dir, 1);
    QVERIFY(qAbs(r.waypoints().first().coord - 30.0) < 1e-9);
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void TestRoute::switchPositions()
{
    QTemporaryDir tmp;
    buildSyntheticRoute(tmp.path());

    Route r;
    QVERIFY(r.load(tmp.path()));

    // Конец a и начало b совпадают в точке (100, 100)
    const SwitchData &s1 = r.switches().at(0);
    QVERIFY(s1.hasPos);
    QCOMPARE(s1.pos, QPointF(100.0, 100.0));

    // fwdMinusTrajReversed: b примыкает КОНЦОМ, т.е. в точке (100, 200),
    // а a примыкает концом в (100, 100) - это разрыв 100 м
    const SwitchData &rev = r.switches().at(2);
    QVERIFY(rev.hasPos);

    bool gap_reported = false;

    for (const QString &w : r.warnings())
    {
        if (w.contains("s_rev") && w.contains("разрыв"))
        {
            gap_reported = true;
        }
    }

    QVERIFY2(gap_reported, "ожидалось замечание о разрыве у стрелки s_rev");
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void TestRoute::warnsAboutUnknownTrajectory()
{
    QTemporaryDir tmp;
    buildSyntheticRoute(tmp.path());

    Route r;
    QVERIFY(r.load(tmp.path()));

    bool found = false;

    for (const QString &w : r.warnings())
    {
        if (w.contains("ghost"))
        {
            found = true;
        }
    }

    QVERIFY2(found, "ожидалось замечание о неизвестной траектории ghost");
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void TestRoute::pointAtInterpolates()
{
    QTemporaryDir tmp;
    buildSyntheticRoute(tmp.path());

    Route r;
    QVERIFY(r.load(tmp.path()));

    const TrajectoryData *a = r.find("a");
    QVERIFY(a != nullptr);

    QPointF tangent;

    // Первый отрезок идёт вдоль +x
    QPointF p = a->pointAt(50.0, &tangent);
    QCOMPARE(p, QPointF(50.0, 0.0));
    QCOMPARE(tangent, QPointF(1.0, 0.0));

    // Второй отрезок идёт вдоль +y
    p = a->pointAt(150.0, &tangent);
    QCOMPARE(p, QPointF(100.0, 50.0));
    QCOMPARE(tangent, QPointF(0.0, 1.0));

    // За пределами диапазона координата ограничивается
    QCOMPARE(a->pointAt(-10.0), QPointF(0.0, 0.0));
    QCOMPARE(a->pointAt(1e9), QPointF(100.0, 100.0));
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void TestRoute::nearestProjectsToTrajectory()
{
    QTemporaryDir tmp;
    buildSyntheticRoute(tmp.path());

    Route r;
    QVERIFY(r.load(tmp.path()));

    // Рядом с первым отрезком a
    Route::Hit hit = r.nearest(QPointF(50.0, 3.0), 10.0);
    QVERIFY(hit.valid);
    QCOMPARE(hit.trajectory, QString("a"));
    QVERIFY(qAbs(hit.coord - 50.0) < 1e-9);
    QVERIFY(qAbs(hit.distance - 3.0) < 1e-9);

    // Ближе к b, чем ко второму отрезку a
    hit = r.nearest(QPointF(103.0, 150.0), 10.0);
    QVERIFY(hit.valid);
    QCOMPARE(hit.trajectory, QString("b"));
    QVERIFY(qAbs(hit.coord - 50.0) < 1e-9);

    // Слишком далеко
    hit = r.nearest(QPointF(500.0, 500.0), 10.0);
    QVERIFY(!hit.valid);
}

//------------------------------------------------------------------------------
// Проверка на реальном маршруте RRS (если задан RRS_ROUTE_DIR)
//------------------------------------------------------------------------------
void TestRoute::loadsRealRoute()
{
    const QString dir = qEnvironmentVariable("RRS_ROUTE_DIR");

    if (dir.isEmpty())
    {
        QSKIP("RRS_ROUTE_DIR не задан: проверка на реальном маршруте пропущена");
    }

    Route r;
    QVERIFY2(r.load(dir), qPrintable(r.errorString()));

    qInfo() << "Маршрут:" << r.title();
    qInfo() << "Траекторий:" << r.trajectories().size()
            << "Стрелок:" << r.switches().size()
            << "Станций:" << r.stations().size()
            << "Путевых точек:" << r.waypoints().size();
    qInfo() << "Габарит:" << r.bounds();
    qInfo() << "Замечаний:" << r.warnings().size();

    for (const QString &w : r.warnings())
    {
        qInfo().noquote() << "  " << w;
    }

    // Числа получены независимым подсчётом по файлам маршрута
    QCOMPARE(r.trajectories().size(), 334);
    QCOMPARE(r.switches().size(), 279);
    QCOMPARE(r.stations().size(), 4);

    // Траектория из двух точек: (-1.75, 7276.2797..) -> (-1.75, 9212.5297..)
    const TrajectoryData *t = r.find("track_a-b_1-3");
    QVERIFY(t != nullptr);
    QVERIFY(qAbs(t->length - 1936.25) < 0.01);

    // Для каждой траектории середина должна находиться поиском ближайшей
    // траектории (проверка согласованности pointAt и nearest)
    for (const TrajectoryData &traj : r.trajectories())
    {
        const QPointF mid = traj.pointAt(traj.length / 2.0);
        const Route::Hit hit = r.nearest(mid, 0.5);

        QVERIFY2(hit.valid, qPrintable(traj.name));
        QVERIFY2(hit.distance < 0.01, qPrintable(traj.name));
    }

    // В данных маршрута есть путевые точки с несуществующими траекториями
    // (например track_a_p2k). Это не ошибка загрузчика, но каждая такая ссылка
    // обязана попасть в замечания
    int dangling = 0;

    for (const WaypointData &wp : r.waypoints())
    {
        if (r.find(wp.trajectory) != nullptr)
        {
            continue;
        }

        ++dangling;

        bool reported = false;

        for (const QString &w : r.warnings())
        {
            if (w.contains(wp.trajectory) && w.contains(wp.title))
            {
                reported = true;
            }
        }

        QVERIFY2(reported, qPrintable("нет замечания для " + wp.trajectory));
    }

    qInfo() << "Путевых точек с несуществующей траекторией:" << dangling;
}

QTEST_APPLESS_MAIN(TestRoute)

#include "test-route.moc"
