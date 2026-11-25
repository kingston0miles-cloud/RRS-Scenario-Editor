#include    "scenario/stationroute.h"

#include    <QObject>

#include    <algorithm>
#include    <cmath>

namespace rse
{

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
TrajKind trajectoryKind(const QString &name)
{
    static const QString prefix = QStringLiteral("track_");

    if (!name.startsWith(prefix))
    {
        return TrajKind::Haul;
    }

    const QString rest = name.mid(prefix.size());
    const int u = rest.indexOf(QLatin1Char('_'));
    const QString token = (u >= 0) ? rest.left(u) : rest;

    return token.contains(QLatin1Char('-')) ? TrajKind::Haul : TrajKind::Station;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
EndSignal signalAtEnd(const Route &route, const QString &trajectory, int dir)
{
    EndSignal res;

    const QVector<Route::Endpoint> next = route.crossings(trajectory, dir > 0);

    if (next.isEmpty())
    {
        return res;
    }

    const SwitchData *sw = route.findSwitch(next.first().switchName);

    if (sw == nullptr)
    {
        return res;
    }

    // Симулятор берёт у стрелки светофор по направлению движения по траектории
    res.liter = (dir > 0) ? sw->signalFwd : sw->signalBwd;
    res.role = signalRoleOf((dir > 0) ? sw->modelFwd : sw->modelBwd);
    res.sw = sw->name;

    if (res.liter.isEmpty())
    {
        res.role = SignalRole::None;
        res.sw.clear();
    }

    return res;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
QVector<PathStep> stepsOf(const Route &route, const PathResult &path)
{
    QVector<PathStep> steps;

    for (const PathSegment &seg : path.segments)
    {
        if (!steps.isEmpty() && steps.last().trajectory == seg.trajectory)
        {
            continue;
        }

        PathStep st;
        st.trajectory = seg.trajectory;
        st.dir = seg.dir;

        const TrajectoryData *t = route.find(seg.trajectory);
        st.length = (t != nullptr) ? t->length : 0.0;

        steps.push_back(st);
    }

    return steps;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
QVector<StationZone> detectZones(const Route &route, const QVector<PathStep> &steps)
{
    // Станция находится по светофорам, а не по именам траекторий (в маршрутах из редактора
    // карт все траектории называются route1_NNNN_NNNN): входной светофор открывает зону,
    // первый после него выходной закрывает. Подход - перед линейными светофорами, ведущими
    // ко входному, уход - сразу за выходным
    QVector<SignalRole> roles(steps.size(), SignalRole::None);

    for (int i = 0; i < steps.size(); ++i)
    {
        roles[i] = signalAtEnd(route, steps[i].trajectory, steps[i].dir).role;
    }

    QVector<StationZone> zones;

    for (int e = 0; e < steps.size(); ++e)
    {
        if (roles[e] != SignalRole::Enter)
        {
            continue;
        }

        StationZone z;
        z.enterStep = e;
        z.first = e + 1;

        // Выходной светофор: первый после входного, но до входного следующей станции
        int x = -1;

        for (int j = e + 1; j < steps.size() && roles[j] != SignalRole::Enter; ++j)
        {
            if (roles[j] == SignalRole::Exit)
            {
                x = j;
                break;
            }
        }

        z.exitStep = x;
        z.last = (x >= 0) ? x : std::min<int>(e + 1, steps.size() - 1);

        // Линейные светофоры перед входным: подход берётся перед ними
        int k = 0;

        while (e - 1 - k >= 0 && roles[e - 1 - k] == SignalRole::Line && k < 2)
        {
            ++k;
        }

        z.approachStep = std::max(0, e - std::max(1, k));

        if (z.last + 1 < steps.size())
        {
            z.removalStep = z.last + 1;
        }

        const QString &name = steps[std::min<int>(z.first, steps.size() - 1)].trajectory;
        z.token = name;
        zones.push_back(z);
    }

    return zones;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
bool AutopilotReport::ok() const
{
    if (dir == 0 || !problems.isEmpty())
    {
        return false;
    }

    for (const StationRouteCheck &r : rows)
    {
        if (!r.arrivalOk || !r.departureOk)
        {
            return false;
        }
    }

    return true;
}

namespace
{

QStringList stationSignals(const Route &route, const GameRoute &g, QVector<RouteSignalMark> *marks)
{
    QStringList out;

    for (int i = 0; i + 1 < g.steps.size(); ++i)
    {
        const EndSignal s = signalAtEnd(route, g.steps[i].trajectory, g.steps[i].dir);

        if (s.role == SignalRole::None || s.role == SignalRole::Shunt)
        {
            continue;
        }

        out << s.liter;

        if (marks != nullptr)
        {
            RouteSignalMark m;
            m.sw = s.sw;
            m.traj = g.steps[i].trajectory;
            m.liter = s.liter;
            m.role = s.role;
            m.station = (s.role == SignalRole::Enter || s.role == SignalRole::Route
                         || s.role == SignalRole::Exit);
            *marks << m;
        }
    }

    return out;
}

bool branches(const SwitchData &sw)
{
    return !sw.fwdMinus.isEmpty() || !sw.bwdMinus.isEmpty() || !sw.fwdMinusReversed.isEmpty();
}

} // namespace

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
AutopilotReport simulateAutopilot(const Route &route, const QVector<AutoStation> &stations)
{
    AutopilotReport rep;

    if (stations.size() < 2)
    {
        rep.problems << QObject::tr("В графике меньше двух станций.");
        return rep;
    }

    // Направление: как в автопилоте - сначала "туда" от текущей траектории до конечной станции
    const QString &first = stations.first().target;
    const QString &last = stations.last().target;

    if (gameRoute(route, first, last, 1).found)
    {
        rep.dir = 1;
    }
    else if (gameRoute(route, first, last, -1).found)
    {
        rep.dir = -1;
    }
    else
    {
        rep.problems << QObject::tr("От «%1» до «%2» маршрута нет ни в одном направлении: "
                                    "автопилот сбросит график.").arg(first, last);
        return rep;
    }

    const int dir = rep.dir;

    auto append = [&rep](const GameRoute &g)
    {
        for (const GameRouteStep &st : g.steps)
        {
            if (rep.chain.isEmpty() || rep.chain.last() != st.trajectory)
            {
                rep.chain << st.trajectory;
            }
        }
    };

    for (int k = 0; k < stations.size(); ++k)
    {
        const AutoStation &st = stations[k];
        StationRouteCheck row;
        row.station = st.name;

        // приём
        if (k > 0 && !st.approach.isEmpty())
        {
            const GameRoute g = gameRoute(route, st.approach, st.target, dir);

            if (!g.found)
            {
                row.arrivalOk = false;
                row.problems << QObject::tr("Маршрут приёма «%1» → «%2» в направлении %3 не строится: "
                                            "автопилот не откроет светофоры. Выберите другой подход "
                                            "или путь станции.")
                                    .arg(st.approach, st.target).arg(dir > 0 ? "+1" : "-1");
            }
            else
            {
                row.arrivalHops = g.hops();
                row.arrivalSignals = stationSignals(route, g, &row.arrivalMarks);

                if (row.arrivalSignals.isEmpty())
                {
                    row.problems << QObject::tr("Маршрут приёма не содержит станционных светофоров: "
                                                "подход слишком близко к станции или не там.");
                }
            }

            // перегон от ухода предыдущей станции до подхода этой
            const QString &rem = stations[k - 1].removal;

            if (!rem.isEmpty() && rem != st.approach)
            {
                const GameRoute h = gameRoute(route, rem, st.approach, dir);

                if (!h.found)
                {
                    row.arrivalOk = false;
                    row.problems << QObject::tr("От ухода «%1» до подхода «%2» проехать нельзя "
                                                "(направление %3).").arg(rem, st.approach).arg(dir > 0 ? "+1" : "-1");
                }
                else
                {
                    append(h);

                    QStringList junctions;

                    for (int i = 1; i < h.steps.size(); ++i)
                    {
                        const SwitchData *sw = route.findSwitch(h.steps[i].viaSwitch);

                        if (sw != nullptr && branches(*sw))
                        {
                            junctions << sw->name;
                        }
                    }

                    if (!junctions.isEmpty())
                    {
                        row.problems << QObject::tr("На перегоне до станции есть стрелки (%1): маршрутом "
                                                    "они не переводятся, поезд пойдёт по их текущему "
                                                    "положению.").arg(junctions.mid(0, 4).join(QStringLiteral(", ")));
                    }
                }
            }

            append(g);
        }
        else if (k > 0)
        {
            // Без подхода маршрут приёма не строится: станция полагается на предыдущие маршруты
            if (rep.chain.isEmpty() || rep.chain.last() != st.target)
            {
                rep.chain << st.target;
            }
        }
        else
        {
            rep.chain << st.target;
        }

        // отправление
        if (k + 1 < stations.size() && !st.removal.isEmpty())
        {
            const GameRoute g = gameRoute(route, st.target, st.removal, dir);

            if (!g.found)
            {
                row.departureOk = false;
                row.problems << QObject::tr("Маршрут отправления «%1» → «%2» в направлении %3 не строится: "
                                            "выходной светофор не откроется.")
                                    .arg(st.target, st.removal).arg(dir > 0 ? "+1" : "-1");
            }
            else
            {
                row.departureHops = g.hops();
                row.departureSignals = stationSignals(route, g, &row.departureMarks);

                if (row.departureSignals.isEmpty())
                {
                    row.problems << QObject::tr("Маршрут отправления не содержит станционных светофоров: "
                                                "уход слишком близко к станции.");
                }

                append(g);
            }
        }

        rep.rows.push_back(row);
    }

    return rep;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
bool haulHasSwitches(const Route &route, const QVector<PathStep> &steps, int from, int to)
{
    for (int i = std::max(0, from); i <= to && i < steps.size(); ++i)
    {
        const QVector<Route::Endpoint> next = route.crossings(steps[i].trajectory, steps[i].dir > 0);

        if (next.isEmpty())
        {
            continue;
        }

        const SwitchData *sw = route.findSwitch(next.first().switchName);

        if (sw != nullptr && branches(*sw))
        {
            return true;
        }
    }

    return false;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
int firstDivergence(const QStringList &chain, const QVector<PathStep> &intended)
{
    // каждая траектория игры должна встретиться в задуманном пути в том же порядке
    int pos = 0;

    for (int i = 0; i < chain.size(); ++i)
    {
        bool found = false;

        for (int j = pos; j < intended.size(); ++j)
        {
            if (intended[j].trajectory == chain[i])
            {
                pos = j + 1;
                found = true;
                break;
            }
        }

        if (!found)
        {
            return i;
        }
    }

    return -1;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
QVector<StationTrack> stationTracks(const Route &route, const QPointF &stationPos, double radius)
{
    QVector<StationTrack> out;

    for (auto it = route.trajectories().constBegin(); it != route.trajectories().constEnd(); ++it)
    {
        const TrajectoryData &t = it.value();

        if (trajectoryKind(t.name) != TrajKind::Station || t.length < 50.0)
        {
            continue;
        }

        StationTrack best;
        best.trajectory = t.name;
        best.distance = radius * radius + 1.0;

        for (double c = 0.0; c <= t.length; c += 10.0)
        {
            const QPointF d = t.pointAt(c) - stationPos;
            const double dd = d.x() * d.x() + d.y() * d.y();

            if (dd < best.distance)
            {
                best.distance = dd;
                best.coord = c;
            }
        }

        if (best.distance <= radius * radius)
        {
            best.distance = std::sqrt(best.distance);
            out.push_back(best);
        }
    }

    std::sort(out.begin(), out.end(), [](const StationTrack &a, const StationTrack &b)
    {
        return a.distance < b.distance;
    });

    return out;
}

} // namespace rse
