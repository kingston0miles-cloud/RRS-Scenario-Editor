#include    <QtTest>

#include    <QDir>
#include    <QFile>
#include    <QFileInfo>
#include    <QTemporaryDir>

#include    "route/route.h"
#include    "scenario/stationroute.h"
#include    "scenario/autotriggers.h"
#include    "test-utils.h"

using namespace rse;

namespace
{

} // namespace

class TestStationRoute : public QObject
{
    Q_OBJECT

private slots:

    void kinds()
    {
        QCOMPARE(trajectoryKind("track_bat_1"), TrajKind::Station);
        QCOMPARE(trajectoryKind("track_zar-1352km_n-1"), TrajKind::Haul);
        QCOMPARE(trajectoryKind("route1_0012_0013"), TrajKind::Haul);
    }

    void classes()
    {
        QCOMPARE(guessTrainClass("073"), TrainClass::Express);
        QCOMPARE(guessTrainClass("259A"), TrainClass::Passenger);
        QCOMPARE(guessTrainClass("6813"), TrainClass::Suburban);
        QCOMPARE(guessTrainClass("2004"), TrainClass::Freight);
        QCOMPARE(guessTrainClass("9101"), TrainClass::Service);
        QVERIFY(classPriority(TrainClass::Express) < classPriority(TrainClass::Passenger));
        QVERIFY(classPriority(TrainClass::Passenger) < classPriority(TrainClass::Suburban));
        QVERIFY(classPriority(TrainClass::Suburban) < classPriority(TrainClass::Freight));

        PlacedTrain t;
        t.name = "2004";
        QCOMPARE(effectiveClass(t), TrainClass::Freight);
        t.trainClass = static_cast<int>(TrainClass::Express);
        QCOMPARE(effectiveClass(t), TrainClass::Express);
    }

    // Светофоры маршрутов: маршрут помнит, на какой стрелке какой светофор откроется
    // (для столбца «Маршрут» и подсветки на карте, этап 19)
    void signalMarks()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString root = dir.path();

        // T1 - станция, H - перегон, T2 - станция; светофоры по ходу движения +1
        writeFile(root + "/topology/trajectories/T1.traj", "0 0 0 0\n0 400 0 400\n");
        writeFile(root + "/topology/trajectories/H.traj", "0 400 0 0\n0 900 0 500\n");
        writeFile(root + "/topology/trajectories/T2.traj", "0 900 0 0\n0 1300 0 400\n");
        writeFile(root + "/topology/topology.xml",
                  "<?xml version=\"1.0\"?>\n<Config>\n"
                  "<Switch><Name>in</Name><bwdPlusTraj>T1</bwdPlusTraj><fwdPlusTraj>H</fwdPlusTraj>"
                  "<SignalLiterFwd>Ч</SignalLiterFwd><SignalModelFwd>vhod-entr</SignalModelFwd></Switch>\n"
                  "<Switch><Name>out</Name><bwdPlusTraj>H</bwdPlusTraj><fwdPlusTraj>T2</fwdPlusTraj>"
                  "<SignalLiterFwd>Н</SignalLiterFwd><SignalModelFwd>vihod-exit</SignalModelFwd></Switch>\n"
                  "</Config>\n");

        Route r;
        QVERIFY2(r.load(root), qPrintable(r.errorString()));

        EndSignal s = signalAtEnd(r, "T1", 1);
        QCOMPARE(s.role, SignalRole::Enter);
        QCOMPARE(s.liter, QStringLiteral("Ч"));
        QCOMPARE(s.sw, QStringLiteral("in"));

        // Отправление с A открывает входной «Ч», приём на B - выходной «Н»
        QVector<AutoStation> st;
        st << AutoStation{ QStringLiteral("A"), QStringLiteral("T1"), QString(), QStringLiteral("H") };
        st << AutoStation{ QStringLiteral("B"), QStringLiteral("T2"), QStringLiteral("H"), QString() };

        const AutopilotReport rep = simulateAutopilot(r, st);
        QCOMPARE(rep.dir, 1);
        QVERIFY2(rep.ok(), qPrintable(rep.problems.join("; ")));

        QCOMPARE(rep.rows[0].departureSignals, QStringList{ QStringLiteral("Ч") });
        QCOMPARE(rep.rows[1].arrivalSignals, QStringList{ QStringLiteral("Н") });

        QCOMPARE(rep.rows[0].departureMarks.size(), 1);
        QCOMPARE(rep.rows[0].departureMarks[0].sw, QStringLiteral("in"));
        QCOMPARE(rep.rows[0].departureMarks[0].liter, QStringLiteral("Ч"));

        QCOMPARE(rep.rows[1].arrivalMarks.size(), 1);
        QCOMPARE(rep.rows[1].arrivalMarks[0].sw, QStringLiteral("out"));
        QCOMPARE(rep.rows[1].arrivalMarks[0].role, SignalRole::Exit);
        QVERIFY(rep.rows[1].arrivalMarks[0].station);
    }

    // Реальная топология Ростов - Тимашевская (переменная RRS_ROSTOV_DIR)
    void realRoute()
    {
        const QString dir = qEnvironmentVariable("RRS_ROSTOV_DIR");

        if (dir.isEmpty())
        {
            QSKIP("RRS_ROSTOV_DIR не задана");
        }

        Route r;
        QVERIFY(r.load(dir));

        // Реальный график 259A, поле Approach/Removal Староминской и Высочино
        QVector<AutoStation> st;
        st << AutoStation{"A", "track_zar-1352km_n-1", "", ""};
        QVERIFY(!gameRoute(r, "track_zar-1352km_n-1", "track_zar-1352km_n-1", 1).steps.isEmpty());
        QVERIFY(simulateAutopilot(r, st).dir == 0);
    }
};

QTEST_MAIN(TestStationRoute)
#include "test-stationroute.moc"
