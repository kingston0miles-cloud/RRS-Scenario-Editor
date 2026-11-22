#include    <QtTest>

#include    <QDir>
#include    <QFile>
#include    <QTemporaryDir>

#include    "scenario/scenario.h"
#include    "scenario/pathfinder.h"

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
// Маршрут вдоль оси Y:
//   A (0,0)->(0,100), стрелка s1: из A вперёд в B (плюс) или C (минус)
//   B (0,100)->(0,200), C (0,100)->(50,200)
//   стрелка s3: B своим концом, к ней же концом примыкает F
//   F (0,300)->(0,200) - идёт «обратно»
//------------------------------------------------------------------------------
void buildRoute(const QString &root)
{
    writeFile(root + "/topology/trajectories/A.traj", "0 0 0 1000\n0 100 0 1100\n");
    writeFile(root + "/topology/trajectories/B.traj", "0 100 0 1100\n0 200 0 1200\n");
    writeFile(root + "/topology/trajectories/C.traj", "0 100 0 1100\n50 200 0 1212\n");
    writeFile(root + "/topology/trajectories/F.traj", "0 300 0 1300\n0 200 0 1200\n");

    writeFile(root + "/topology/topology.xml",
              "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
              "<Config>\n"
              "  <Switch><Name>s1</Name><bwdPlusTraj>A</bwdPlusTraj>"
              "<fwdPlusTraj>B</fwdPlusTraj><fwdMinusTraj>C</fwdMinusTraj></Switch>\n"
              "  <Switch><Name>s3</Name><bwdPlusTraj>B</bwdPlusTraj>"
              "<fwdMinusTrajReversed>F</fwdMinusTrajReversed></Switch>\n"
              "</Config>\n");
}

PlacedTrain makeTrain(const QString &name, const QString &traj, double coord, int dir, double len)
{
    PlacedTrain t;
    t.name = name;
    t.title = name;
    t.trajectory = traj;
    t.coord = coord;
    t.dir = dir;
    t.length = len;

    return t;
}

double total(const Footprint &fp)
{
    double s = 0.0;

    for (const FootprintSegment &seg : fp.segments)
    {
        s += seg.to - seg.from;
    }

    return s;
}

} // namespace

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
class TestScenario : public QObject
{
    Q_OBJECT

private:

    QTemporaryDir dir_;
    Route route_;

private slots:

    void initTestCase();

    void crossingsAcrossSwitches();
    void orientationBySignals();
    void subPolyline();
    void footprintOnOneTrajectory();
    void footprintAcrossSwitch();
    void footprintDeadEnd();
    void footprintAmbiguousSwitch();
    void footprintThroughReversedTrajectory();
    void overlapAndCoupling();
    void reverseInPlace();
    void uniqueNames();
    void realRouteFootprints();
    void pathAcrossSwitches();
    void pathSameTrajectoryAndErrors();
};

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void TestScenario::initTestCase()
{
    QVERIFY(dir_.isValid());
    buildRoute(dir_.path());
    QVERIFY2(route_.load(dir_.path()), qPrintable(route_.errorString()));
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void TestScenario::crossingsAcrossSwitches()
{
    // За концом A: B (плюс) и C (минус) - положение стрелки неизвестно
    const auto a_end = route_.crossings("A", true);
    QCOMPARE(a_end.size(), 2);
    QCOMPARE(a_end[0].trajectory, QString("B"));
    QVERIFY(!a_end[0].atEnd);
    QCOMPARE(a_end[1].trajectory, QString("C"));

    // За началом A ничего нет
    QVERIFY(route_.crossings("A", false).isEmpty());

    // Из B назад - в A (A примыкает к стрелке концом)
    const auto b_start = route_.crossings("B", false);
    QCOMPARE(b_start.size(), 1);
    QCOMPARE(b_start[0].trajectory, QString("A"));
    QVERIFY(b_start[0].atEnd);

    // За концом B - конец F
    const auto b_end = route_.crossings("B", true);
    QCOMPARE(b_end.size(), 1);
    QCOMPARE(b_end[0].trajectory, QString("F"));
    QVERIFY(b_end[0].atEnd);
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void TestScenario::orientationBySignals()
{
    // В основном маршруте светофоров нет - направление неизвестно
    QCOMPARE(route_.orientation("A"), 0);
    QCOMPARE(route_.orientation("F"), 0);
    QCOMPARE(route_.orientation("нет такой"), 0);

    QTemporaryDir tmp;
    const QString t = tmp.path() + "/topology/trajectories/";

    auto traj = [&](const QString &name, double y0, double y1)
    {
        writeFile(t + name + ".traj", QString("0 %1 0 0\n0 %2 0 %3\n").arg(y0).arg(y1).arg(qAbs(y1 - y0)));
    };

    traj("P", 0, 100);    traj("Q", 100, 200);   traj("R", 200, 300);
    traj("T1", 600, 700); traj("T2", 700, 800);
    traj("U1", 900, 1000); traj("U2", 1000, 1100);
    traj("V1", 1200, 1300); traj("V2", 1300, 1400);
    traj("X", 1400, 1500); traj("Z", 1600, 1500);

    auto sw = [](const QString &name, const QString &bwd, const QString &fwd, const QString &sig,
                 bool fwd_reversed = false)
    {
        return QString("<Switch><Name>%1</Name><bwdPlusTraj>%2</bwdPlusTraj>%3%4</Switch>\n")
            .arg(name, bwd,
                 fwd_reversed ? QString("<fwdMinusTrajReversed>%1</fwdMinusTrajReversed>").arg(fwd)
                              : QString("<fwdPlusTraj>%1</fwdPlusTraj>").arg(fwd),
                 sig);
    };

    writeFile(tmp.path() + "/topology/topology.xml",
              "<Config>\n"
              + sw("j1", "P", "Q", "")                                              // простой стык без светофоров
              + sw("j2", "Q", "R", "<SignalLiterBwd>Ч1</SignalLiterBwd>")           // +1 через стрелку
              + sw("j3", "T1", "T2", "<SignalLiterFwd>Н</SignalLiterFwd>")          // -1 через стрелку
              + sw("j4", "U1", "U2", "<SignalLiterBwd>ЧД</SignalLiterBwd>")         // на неправильный путь
              + sw("j5", "V1", "V2", "<SignalLiterBwd>М7</SignalLiterBwd>")         // маневровый
              + sw("j6", "X", "Z", "<SignalLiterBwd>Ч5</SignalLiterBwd>", true)     // Z примыкает концом
              + "</Config>\n");

    Route r;
    QVERIFY2(r.load(tmp.path()), qPrintable(r.errorString()));

    // Светофор Ч1 вперёд через стрелку: Q и R «туда», P - по цепочке через простой стык
    QCOMPARE(r.orientation("Q"), 1);
    QCOMPARE(r.orientation("R"), 1);
    QCOMPARE(r.orientation("P"), 1);

    // Литера Fwd - светофор в обратную сторону: «обратно»
    QCOMPARE(r.orientation("T1"), -1);
    QCOMPARE(r.orientation("T2"), -1);

    // Только светофор на неправильный путь (…Д): правильное направление обратное
    QCOMPARE(r.orientation("U1"), -1);
    QCOMPARE(r.orientation("U2"), -1);

    // Маневровые светофоры направления не задают
    QCOMPARE(r.orientation("V1"), 0);
    QCOMPARE(r.orientation("V2"), 0);

    // Траектория Z примыкает концом: направление через стрелку - против неё
    QCOMPARE(r.orientation("X"), 1);
    QCOMPARE(r.orientation("Z"), -1);
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void TestScenario::subPolyline()
{
    const TrajectoryData *c = route_.find("C");
    QVERIFY(c != nullptr);

    const QVector<QPointF> mid = c->subPolyline(c->length * 0.25, c->length * 0.75);
    QCOMPARE(mid.size(), 2);
    QVERIFY(qAbs(mid.first().x() - 12.5) < 0.1);
    QVERIFY(qAbs(mid.last().x() - 37.5) < 0.1);

    // Весь участок и обратный порядок аргументов
    QCOMPARE(c->subPolyline(c->length, 0.0).size(), 2);
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void TestScenario::footprintOnOneTrajectory()
{
    const Footprint fp = computeFootprint(route_, "A", 60.0, 1, 30.0);

    QCOMPARE(fp.segments.size(), 1);
    QCOMPARE(fp.segments[0].trajectory, QString("A"));
    QVERIFY(qAbs(fp.segments[0].from - 30.0) < 1e-6);
    QVERIFY(qAbs(fp.segments[0].to - 60.0) < 1e-6);
    QVERIFY(!fp.deadEnd);
    QVERIFY(!fp.ambiguous);
    QCOMPARE(fp.tailMove, -1);
    QVERIFY(qAbs(fp.tailCoord - 30.0) < 1e-6);

    // Голова смотрит к началу - хвост ближе к концу
    const Footprint rev = computeFootprint(route_, "A", 40.0, -1, 30.0);
    QVERIFY(qAbs(rev.segments[0].from - 40.0) < 1e-6);
    QVERIFY(qAbs(rev.segments[0].to - 70.0) < 1e-6);
    QCOMPARE(rev.tailMove, 1);
}

//------------------------------------------------------------------------------
// Хвост уходит назад через стрелку с B на A
//------------------------------------------------------------------------------
void TestScenario::footprintAcrossSwitch()
{
    const Footprint fp = computeFootprint(route_, "B", 20.0, 1, 50.0);

    QCOMPARE(fp.segments.size(), 2);
    QCOMPARE(fp.segments[0].trajectory, QString("B"));
    QCOMPARE(fp.segments[1].trajectory, QString("A"));
    QVERIFY(qAbs(fp.segments[1].from - 70.0) < 1e-6);
    QVERIFY(qAbs(fp.segments[1].to - 100.0) < 1e-6);
    QVERIFY(qAbs(total(fp) - 50.0) < 1e-6);
    QVERIFY(!fp.deadEnd);
    QCOMPARE(fp.tailTrajectory, QString("A"));
    QCOMPARE(fp.tailMove, -1);
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void TestScenario::footprintDeadEnd()
{
    // A начинается в тупике: хвост 30 м от головы на 10-м метре
    const Footprint fp = computeFootprint(route_, "A", 10.0, 1, 30.0);

    QVERIFY(fp.deadEnd);
    QVERIFY(qAbs(fp.overhang - 20.0) < 1e-6);
    QVERIFY(qAbs(total(fp) - 10.0) < 1e-6);
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void TestScenario::footprintAmbiguousSwitch()
{
    // Голова к началу, хвост выходит за конец A, где расходятся B и C
    const Footprint fp = computeFootprint(route_, "A", 90.0, -1, 30.0);

    QVERIFY(fp.ambiguous);
    QCOMPARE(fp.segments.size(), 2);
    QCOMPARE(fp.segments[1].trajectory, QString("B"));
    QVERIFY(qAbs(fp.segments[1].to - 20.0) < 1e-6);
}

//------------------------------------------------------------------------------
// За концом B продолжается F, примыкающая к стрелке своим концом: въезд
// с конца, дальше едем к началу F
//------------------------------------------------------------------------------
void TestScenario::footprintThroughReversedTrajectory()
{
    const Footprint fp = computeFootprint(route_, "B", 95.0, -1, 20.0);

    QCOMPARE(fp.segments.size(), 2);
    QCOMPARE(fp.segments[0].trajectory, QString("B"));
    QCOMPARE(fp.segments[1].trajectory, QString("F"));
    QVERIFY(qAbs(fp.segments[1].from - 85.0) < 1e-6);
    QVERIFY(qAbs(fp.segments[1].to - 100.0) < 1e-6);
    QCOMPARE(fp.tailMove, -1);
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void TestScenario::overlapAndCoupling()
{
    Scenario sc;
    sc.add(makeTrain("T1", "A", 60.0, 1, 30.0));   // [30, 60]

    // Пересечение на 10 м
    PlacementCheck chk = sc.check(route_, makeTrain("T2", "A", 80.0, 1, 30.0));
    QVERIFY(chk.overlap > 9.9 && chk.overlap < 10.1);
    QVERIFY(chk.hasProblems());
    QCOMPARE(chk.overlapWith, QStringList() << "T1");

    // Зазор 5 м: не пересекаются, но в зоне автосцепки
    chk = sc.check(route_, makeTrain("T2", "A", 95.0, 1, 30.0));
    QVERIFY(chk.overlap == 0.0);
    QVERIFY(!chk.hasProblems());
    QCOMPARE(chk.nearTrains, QStringList() << "T1");

    // Зазор 30 м: всё чисто
    chk = sc.check(route_, makeTrain("T2", "B", 10.0, 1, 10.0));
    QVERIFY(chk.nearTrains.isEmpty());
    QVERIFY(chk.overlapWith.isEmpty());

    // Поезд не мешает сам себе при редактировании
    chk = sc.check(route_, makeTrain("T1", "A", 62.0, 1, 30.0), 0);
    QVERIFY(chk.overlapWith.isEmpty());

    // Зона сцепки через стрелку: T1 заканчивается на B[0, 5], новый стоит на A,
    // его голова в 3 м от стрелки
    sc.clear();
    sc.add(makeTrain("T1", "B", 5.0, 1, 5.0));
    chk = sc.check(route_, makeTrain("T2", "A", 97.0, 1, 20.0));
    QCOMPARE(chk.nearTrains, QStringList() << "T1");
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void TestScenario::reverseInPlace()
{
    PlacedTrain t = makeTrain("T", "B", 20.0, 1, 50.0);

    ConsistGroup loco;
    loco.config = "loco";
    loco.forward = true;

    ConsistGroup wagon;
    wagon.config = "wagon";
    wagon.forward = true;

    t.groups << loco << wagon;

    const PlacedTrain r = Scenario::reversedInPlace(route_, t);

    // Хвост был на A, 70 м, ехали к началу A, теперь голова там и смотрит к концу A... 
    QCOMPARE(r.trajectory, QString("A"));
    QVERIFY(qAbs(r.coord - 70.0) < 1e-6);
    QCOMPARE(r.dir, -1);
    QCOMPARE(r.groups[0].config, QString("wagon"));
    QVERIFY(!r.groups[0].forward);
    QVERIFY(!r.groups[1].forward);

    // След занимает те же участки
    const Footprint a = computeFootprint(route_, t.trajectory, t.coord, t.dir, t.length);
    const Footprint b = computeFootprint(route_, r.trajectory, r.coord, r.dir, r.length);
    QVERIFY(qAbs(overlapLength(a, b) - 50.0) < 1e-6);

    // Дважды - исходное положение
    const PlacedTrain rr = Scenario::reversedInPlace(route_, r);
    QCOMPARE(rr.trajectory, t.trajectory);
    QVERIFY(qAbs(rr.coord - t.coord) < 1e-6);
    QCOMPARE(rr.dir, t.dir);
    QCOMPARE(rr.groups[0].config, QString("loco"));
    QVERIFY(rr.groups[0].forward);

    // Разворот головы без сдвига
    QCOMPARE(Scenario::withFlippedHead(t).dir, -1);
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void TestScenario::uniqueNames()
{
    Scenario sc;
    QCOMPARE(sc.uniqueName("Поезд"), QString("Поезд 1"));

    sc.add(makeTrain("Поезд 1", "A", 50.0, 1, 10.0));
    sc.add(makeTrain("Поезд 2", "B", 50.0, 1, 10.0));

    QCOMPARE(sc.uniqueName("Поезд"), QString("Поезд 3"));
    QCOMPARE(sc.indexOfName("Поезд 2"), 1);

    sc.remove(0);
    QCOMPARE(sc.size(), 1);
    QCOMPARE(sc.uniqueName("Поезд"), QString("Поезд 1"));
}

//------------------------------------------------------------------------------
// На реальном маршруте: след всегда равен длине состава, пока не упёрся в тупик
//------------------------------------------------------------------------------
void TestScenario::realRouteFootprints()
{
    const QString dir = qEnvironmentVariable("RRS_ROUTE_DIR");

    if (dir.isEmpty())
    {
        QSKIP("RRS_ROUTE_DIR не задан");
    }

    Route r;
    QVERIFY2(r.load(dir), qPrintable(r.errorString()));

    int checked = 0;
    int dead = 0;

    for (const TrajectoryData &t : r.trajectories())
    {
        for (int d : { 1, -1 })
        {
            const Footprint fp = computeFootprint(r, t.name, t.length / 2.0, d, 200.0);

            QVERIFY(!fp.segments.isEmpty());

            if (fp.deadEnd)
            {
                ++dead;
                QVERIFY(qAbs(total(fp) + fp.overhang - 200.0) < 1e-6);
            }
            else
            {
                QVERIFY2(qAbs(total(fp) - 200.0) < 1e-6, qPrintable(t.name));
            }

            ++checked;
        }
    }

    QVERIFY(checked > 600);

    // Не все 200-метровые составы упираются в тупик
    QVERIFY(dead < checked / 2);

    // Ориентация по пикетажу: считаем, сколько траекторий её имеют
    int plus = 0;
    int minus = 0;

    for (const TrajectoryData &t : r.trajectories())
    {
        const int o = r.orientation(t.name);
        plus += (o > 0);
        minus += (o < 0);
    }

    qInfo("«туда»: %d, «обратно»: %d из %d", plus, minus, int(r.trajectories().size()));
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void TestScenario::pathAcrossSwitches()
{
    // A -> B через стрелку s1
    PathResult r = findPath(route_, "A", 10.0, 1, "B", 50.0);
    QVERIFY2(r.found, qPrintable(r.error));
    QCOMPARE(r.segments.size(), 2);
    QCOMPARE(r.segments[0].trajectory, QString("A"));
    QCOMPARE(r.segments[1].trajectory, QString("B"));
    QCOMPARE(r.segments[1].dir, 1);
    QVERIFY(qAbs(r.length - (90.0 + 50.0)) < 0.5);
    QVERIFY(r.switchNames.contains("s1"));

    // A -> C
    r = findPath(route_, "A", 10.0, 1, "C", 20.0);
    QVERIFY(r.found);
    QCOMPARE(r.segments.last().trajectory, QString("C"));

    // A -> B -> F (F примыкает концом, въезд в обратном направлении)
    r = findPath(route_, "A", 0.0, 1, "F", 40.0);
    QVERIFY2(r.found, qPrintable(r.error));
    QCOMPARE(r.segments.last().trajectory, QString("F"));
    QCOMPARE(r.segments.last().dir, -1);
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void TestScenario::pathSameTrajectoryAndErrors()
{
    PathResult r = findPath(route_, "B", 10.0, 1, "B", 60.0);
    QVERIFY(r.found);
    QCOMPARE(r.segments.size(), 1);
    QVERIFY(qAbs(r.length - 50.0) < 1e-6);

    // Цель позади, без разворота не достичь (B назад -> A есть, но C из B не достижим)
    r = findPath(route_, "B", 10.0, 1, "C", 20.0);
    QVERIFY(!r.found);
    QVERIFY(!r.error.isEmpty());

    r = findPath(route_, "nope", 0.0, 1, "B", 20.0);
    QVERIFY(!r.found);
}

QTEST_MAIN(TestScenario)

#include "test-scenario.moc"
