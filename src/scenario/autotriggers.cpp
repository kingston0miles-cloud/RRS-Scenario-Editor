#include    "scenario/autotriggers.h"

#include    "scenario/export.h"
#include    "scenario/pathfinder.h"
#include    "scenario/stationroute.h"

#include    <QObject>
#include    <QRegularExpression>
#include    <QSet>

#include    <algorithm>

namespace rse
{

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
QString trainClassTitle(TrainClass cls)
{
    switch (cls)
    {
    case TrainClass::Auto: return QObject::tr("по номеру поезда");
    case TrainClass::Express: return QObject::tr("скорый / скоростной");
    case TrainClass::Passenger: return QObject::tr("пассажирский");
    case TrainClass::Suburban: return QObject::tr("пригородный");
    case TrainClass::Freight: return QObject::tr("грузовой");
    case TrainClass::Service: return QObject::tr("резервом / служебный");
    }

    return QString();
}

//------------------------------------------------------------------------------
// Нумерация по Инструкции по организации движения поездов: скорые 001-150
// (фирменные 001-098), пассажирские 151-298 и 301-398, скоростные 701-788,
// пригородные 6001-6998 и 6xx с индексами, грузовые 1001-3998 и далее, одиночные
// локомотивы резервом 9001-9998. Это догадка по номеру - категорию можно задать вручную
//------------------------------------------------------------------------------
TrainClass guessTrainClass(const QString &name)
{
    static const QRegularExpression digits(QStringLiteral("(\\d+)"));
    const QRegularExpressionMatch m = digits.match(name);

    if (!m.hasMatch())
    {
        return TrainClass::Freight;
    }

    const int n = m.captured(1).toInt();

    if ((n >= 1 && n <= 150) || (n >= 701 && n <= 788))
    {
        return TrainClass::Express;
    }

    if ((n >= 151 && n <= 398) || (n >= 401 && n <= 698))
    {
        return TrainClass::Passenger;
    }

    if (n >= 6000 && n <= 6999)
    {
        return TrainClass::Suburban;
    }

    if (n >= 9000 && n <= 9999)
    {
        return TrainClass::Service;
    }

    return TrainClass::Freight;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
TrainClass effectiveClass(const PlacedTrain &train)
{
    const TrainClass c = static_cast<TrainClass>(train.trainClass);

    return (c == TrainClass::Auto) ? guessTrainClass(train.name) : c;
}

int classPriority(TrainClass cls)
{
    switch (cls)
    {
    case TrainClass::Express: return 1;
    case TrainClass::Passenger: return 2;
    case TrainClass::Suburban: return 3;
    case TrainClass::Freight: return 4;
    case TrainClass::Service: return 5;
    case TrainClass::Auto: break;
    }

    return 4;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
QString autoEventStation(const QString &title)
{
    static const QRegularExpression re(QStringLiteral("станци[а-яё]+ «([^»]+)»"));
    const QRegularExpressionMatch m = re.match(title);

    return m.hasMatch() ? m.captured(1) : QString();
}

namespace
{

struct Window
{
    int train = 0;
    int station = 0;
    bool arrival = true;

    QString from;
    QString to;
    QStringList trajs;

    int t0 = 0;
    int t1 = 0;

    /// Когда маршрут строится (раньше прохода поезда): с этого момента он занимает пути
    int when = -1;
};

struct TrainRoutes
{
    int index = 0;
    int dir = 0;
    QStringList approach;
    QStringList removal;
    QVector<Window> windows;
};

void timeSpan(const Timetable &tt, const QStringList &trajs, int *t0, int *t1)
{
    bool any = false;

    for (const TimetableSample &s : tt.samples)
    {
        if (!trajs.contains(s.trajectory))
        {
            continue;
        }

        if (!any)
        {
            *t0 = *t1 = s.time;
            any = true;
        }
        else
        {
            *t0 = std::min(*t0, s.time);
            *t1 = std::max(*t1, s.time);
        }
    }
}

QStringList trajectoriesOf(const GameRoute &g)
{
    QStringList l;

    for (const GameRouteStep &s : g.steps)
    {
        l << s.trajectory;
    }

    return l;
}

} // namespace

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
AutoTriggerPlan planAutoTriggers(const Route &route, const Scenario &scenario,
                                 const AutoTriggerOptions &options)
{
    AutoTriggerPlan plan;

    QVector<TrainRoutes> all;

    // 1. Маршруты каждого поезда с расписанием по правилам игры и окна их занятости
    for (int i = 0; i < scenario.size(); ++i)
    {
        const PlacedTrain &t = scenario.trains()[i];

        if (!t.timetable.valid || t.isVirtual() || t.noAutoEvents || t.timetable.stations.size() < 2)
        {
            continue;
        }

        TrainRoutes tr;
        tr.index = i;

        timetableApproachRemoval(route, t, &tr.approach, &tr.removal);

        QVector<AutoStation> as;

        for (int k = 0; k < t.timetable.stations.size(); ++k)
        {
            const TimetableStation &st = t.timetable.stations[k];

            if (!st.approachTrajectory.isEmpty()) tr.approach[k] = st.approachTrajectory;
            if (!st.removalTrajectory.isEmpty()) tr.removal[k] = st.removalTrajectory;

            as.push_back({ st.name, st.targetTrajectory, tr.approach[k], tr.removal[k] });
        }

        const AutopilotReport rep = simulateAutopilot(route, as);
        tr.dir = rep.dir;

        if (rep.dir == 0)
        {
            plan.warnings << QObject::tr("«%1»: %2").arg(t.name, rep.problems.value(0));
            continue;
        }

        for (const StationRouteCheck &row : rep.rows)
        {
            for (const QString &p : row.problems)
            {
                plan.warnings << QObject::tr("«%1», «%2»: %3").arg(t.name, row.station, p);
            }
        }

        for (int k = 0; k < t.timetable.stations.size(); ++k)
        {
            const TimetableStation &st = t.timetable.stations[k];

            if (k > 0 && !tr.approach[k].isEmpty())
            {
                const GameRoute g = gameRoute(route, tr.approach[k], st.targetTrajectory, tr.dir);

                if (g.found)
                {
                    Window w;
                    w.train = i;
                    w.station = k;
                    w.arrival = true;
                    w.from = tr.approach[k];
                    w.to = st.targetTrajectory;
                    w.trajs = trajectoriesOf(g);
                    w.t0 = st.arrival - 180;
                    w.t1 = st.departure;
                    timeSpan(t.timetable, w.trajs, &w.t0, &w.t1);
                    tr.windows.push_back(w);
                }
            }

            if (k + 1 < t.timetable.stations.size() && !tr.removal[k].isEmpty())
            {
                const GameRoute g = gameRoute(route, st.targetTrajectory, tr.removal[k], tr.dir);

                if (g.found)
                {
                    Window w;
                    w.train = i;
                    w.station = k;
                    w.arrival = false;
                    w.from = st.targetTrajectory;
                    w.to = tr.removal[k];
                    w.trajs = trajectoriesOf(g);
                    w.t0 = st.departure - 30;
                    w.t1 = st.departure + 120;
                    timeSpan(t.timetable, w.trajs, &w.t0, &w.t1);
                    tr.windows.push_back(w);
                }
            }
        }

        // Перегон от одной остановки до другой открывается целиком в момент отправления:
        // станции, на которых остановки нет, получают свои маршруты сразу, а станция
        // остановки - только приём; её выходной светофор откроется ко времени отправления
        {
            const int n = t.timetable.stations.size();
            QHash<int, int> counter;

            for (Window &w : tr.windows)
            {
                int s = 0;

                for (int k = 0; k < n; ++k)
                {
                    const bool stop = (k == 0) || (k == n - 1) || t.timetable.stations[k].stops();
                    const bool before = w.arrival ? (k < w.station) : (k <= w.station);

                    if (stop && before)
                    {
                        s = k;
                    }
                }

                const int pos = counter[s]++;
                const int dep = t.timetable.stations[s].departure;

                w.when = std::max(options.startTime, dep - 10) + 3 * pos;
            }
        }

        for (const Window &w : tr.windows)
        {
            RouteClaim c;
            c.train = t.name;
            c.station = t.timetable.stations[w.station].name;
            c.arrival = w.arrival;
            c.trajectories = w.trajs;
            c.t0 = std::min(w.t0, w.when);
            c.t1 = w.t1;
            plan.claims.push_back(c);
        }

        all.push_back(tr);
    }

    // 2. Триггеры
    int gated = 0;
    int plain = 0;

    for (const TrainRoutes &tr : all)
    {
        const PlacedTrain &t = scenario.trains()[tr.index];
        const int prio = classPriority(effectiveClass(t));

        plan.clearApproach[t.name] = QVector<bool>(t.timetable.stations.size(), false);
        plan.clearRemoval[t.name] = QVector<bool>(t.timetable.stations.size(), false);

        for (const Window &w : tr.windows)
        {
            // Кто с более высоким приоритетом претендует на те же пути в то же время
            const Window *rival = nullptr;
            const PlacedTrain *rival_train = nullptr;
            QString rival_traj;

            for (const TrainRoutes &other : all)
            {
                const PlacedTrain &o = scenario.trains()[other.index];

                if (other.index == tr.index || classPriority(effectiveClass(o)) > prio)
                {
                    continue;
                }

                for (const Window &h : other.windows)
                {
                    QString shared;

                    // общая траектория, которую высший поезд освободит последней
                    for (const QString &x : h.trajs)
                    {
                        if (w.trajs.contains(x))
                        {
                            shared = x;
                        }
                    }

                    if (shared.isEmpty())
                    {
                        continue;
                    }

                    const bool overlap = std::min(h.t0, h.when) - options.margin <= w.t1 && h.t1 + options.margin >= std::min(w.t0, w.when);
                    const bool first = h.t0 <= w.t0 + options.margin;

                    // Поезд той же категории пропускается вперёд, если идёт по графику раньше
                    const bool same = classPriority(effectiveClass(o)) == prio;

                    if (same && h.t0 >= w.t0)
                    {
                        continue;
                    }

                    if (overlap && first && (rival == nullptr || h.t1 > rival->t1))
                    {
                        rival = &h;
                        rival_train = &o;
                        rival_traj = shared;
                    }
                }
            }

            const QString who = QObject::tr("«%1»").arg(t.name);
            const TimetableStation &st = t.timetable.stations[w.station];
            ScenarioEvent e;
            EventAction a;
            a.type = ActionType::BuildTrainRoute;
            a.a = w.from;
            a.b = w.to;
            a.dir = tr.dir;

            if (rival != nullptr)
            {
                e.trigger = TriggerType::TrajFree;
                e.traj = rival_traj;
                e.train = rival_train->name;
                e.delaySec = 10;
                e.title = QObject::tr("Очерёдность: %1 (%2) %3 станции «%4» после поезда «%5» (%6)")
                          .arg(t.name, trainClassTitle(effectiveClass(t)),
                               w.arrival ? QObject::tr("принимается на") : QObject::tr("отправляется со"),
                               st.name, rival_train->name,
                               trainClassTitle(effectiveClass(*rival_train)));

                if (w.arrival)
                    plan.clearApproach[t.name][w.station] = true;
                else
                    plan.clearRemoval[t.name][w.station] = true;

                if (w.t0 < rival->t0 && classPriority(effectiveClass(*rival_train)) < prio)
                {
                    plan.warnings << QObject::tr("По графику %1 идёт раньше «%2», но «%2» старше по "
                                                 "категории: %1 будет ждать на станции «%3». Для "
                                                 "пропуска запланируйте стоянку или сдвиньте график.")
                                         .arg(who, rival_train->name, st.name);
                }

                ++gated;
            }
            else if (t.autoMode && !options.forBots)
            {
                continue;
            }
            else if (w.arrival)
            {
                e.trigger = TriggerType::TrajBusy;
                e.traj = w.from;
                e.train = t.name;
                e.title = QObject::tr("Приём: %1 на станцию «%2»").arg(t.name, st.name);
                ++plain;
            }
            else if (st.stops())
            {
                e.trigger = TriggerType::AtTime;
                e.time = std::max(0, st.departure - 10);
                e.title = QObject::tr("Отправление: %1 со станции «%2»").arg(t.name, st.name);
                ++plain;
            }
            else
            {
                e.trigger = TriggerType::TrajBusy;
                e.traj = w.from;
                e.train = t.name;
                e.title = QObject::tr("Пропуск без остановки: %1, станция «%2»").arg(t.name, st.name);
                ++plain;
            }

            e.actions.push_back(a);
            e.owner = t.name;
            plan.events.push_back(e);

            // По времени графика: поезд ещё далеко, а светофоры уже открываются; не вышло -
            // повторы (путь был занят другим составом)
            if (options.byTime && rival == nullptr)
            {
                const int when = w.when;

                if (when >= options.startTime)
                {
                    QVector<int> offsets = { 0 };
                    offsets += options.retries;

                    for (const int off : offsets)
                    {
                        ScenarioEvent te;
                        te.trigger = TriggerType::AtTime;
                        te.time = when + off;
                        te.title = QObject::tr("По графику%1: %2, станция «%3», %4")
                                   .arg(off > 0 ? QObject::tr(" (повтор +%1 с)").arg(off) : QString(),
                                        t.name, st.name,
                                        w.arrival ? QObject::tr("приём") : QObject::tr("отправление"));
                        te.actions.push_back(a);
                        te.owner = t.name;
                        plan.events.push_back(te);
                    }
                }
            }
        }
    }

    plan.notes << QObject::tr("Триггеры по графикам: %1 обычных, %2 с очерёдностью по категории.")
                  .arg(plain).arg(gated);

    return plan;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
QVector<RouteConflict> routeConflicts(const AutoTriggerPlan &plan, const Scenario &scenario)
{
    QVector<RouteConflict> out;
    QSet<QString> seen;

    auto prio = [&scenario](const QString &name)
    {
        for (const PlacedTrain &t : scenario.trains())
        {
            if (t.name == name)
            {
                return classPriority(effectiveClass(t));
            }
        }

        return 0;
    };

    for (int i = 0; i < plan.claims.size(); ++i)
    {
        for (int j = i + 1; j < plan.claims.size(); ++j)
        {
            const RouteClaim &a = plan.claims[i];
            const RouteClaim &b = plan.claims[j];

            if (a.train == b.train || prio(a.train) != prio(b.train)
                || a.t0 > b.t1 || b.t0 > a.t1)
            {
                continue;
            }

            for (const QString &x : a.trajectories)
            {
                if (!b.trajectories.contains(x))
                {
                    continue;
                }

                const QString key = QStringList({ std::min(a.train, b.train), std::max(a.train, b.train), x }).join('|');

                if (seen.contains(key))
                {
                    break;
                }

                seen.insert(key);

                RouteConflict c;
                c.trainA = a.train;
                c.trainB = b.train;
                c.trajectory = x;
                c.t0 = std::max(a.t0, b.t0);
                c.t1 = std::min(a.t1, b.t1);
                out.push_back(c);
                break;
            }
        }
    }

    return out;
}

} // namespace rse
