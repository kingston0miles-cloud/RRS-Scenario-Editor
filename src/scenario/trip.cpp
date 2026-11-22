#include    "scenario/trip.h"

#include    <QObject>
#include    <QtMath>

#include    <algorithm>
#include    <cmath>

namespace rse
{

namespace
{

/// Расстояние от станции до пути, при котором станция считается лежащей на пути, м
constexpr double STATION_RADIUS = 400.0;

/// Шаг выборки точек пути для привязки станций, м
constexpr double STATION_STEP = 20.0;

/// Зазор, ближе которого поезда считаются мешающими друг другу, м
constexpr double CONFLICT_MARGIN = 30.0;

/// Окно по времени для сравнения положений поездов, с
constexpr int CONFLICT_WINDOW = 20;

int ceilMinute(double sec)
{
    return static_cast<int>(std::ceil(sec / 60.0 - 1e-9)) * 60;
}

/// Положение на пути по расстоянию s
struct Located
{
    int segment = 0;
    TripPosition pos;
};

Located locate(const QVector<PathSegment> &segs, double s)
{
    Located r;

    if (segs.isEmpty())
    {
        return r;
    }

    double acc = 0.0;
    int idx = 0;

    for (; idx < segs.size(); ++idx)
    {
        const double len = segs[idx].length();

        if ((s <= acc + len) || (idx == segs.size() - 1))
        {
            break;
        }

        acc += len;
    }

    const PathSegment &seg = segs[idx];
    const double d = std::clamp(s - acc, 0.0, seg.length());

    r.segment = idx;
    r.pos.trajectory = seg.trajectory;
    r.pos.dir = seg.dir;
    r.pos.coord = seg.from + seg.dir * d;

    return r;
}

/// Диапазон координат, занятый составом с головой в coord, идущим в dir
void spanOf(double coord, int dir, double length, double *lo, double *hi)
{
    if (dir > 0)
    {
        *lo = coord - length;
        *hi = coord;
    }
    else
    {
        *lo = coord;
        *hi = coord + length;
    }
}

} // namespace

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
TripPosition TripPlan::position(double s) const
{
    return locate(path.segments, s).pos;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
double defaultReserveMin(double run_time_sec)
{
    return std::max(2.0, std::ceil(run_time_sec * 0.1 / 60.0));
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
QString formatClock(int sec)
{
    sec = ((sec % 86400) + 86400) % 86400;

    return QStringLiteral("%1:%2:%3")
            .arg(sec / 3600, 2, 10, QLatin1Char('0'))
            .arg((sec / 60) % 60, 2, 10, QLatin1Char('0'))
            .arg(sec % 60, 2, 10, QLatin1Char('0'));
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
TripPlan buildTripPlan(const Route &route, const VehicleCatalog &catalog, const Scenario &scenario,
                       int train_index, const QString &traj, double coord)
{
    TripPlan plan;

    if ((train_index < 0) || (train_index >= scenario.size()))
    {
        plan.error = QObject::tr("Поезд не выбран");
        return plan;
    }

    const PlacedTrain &train = scenario.trains()[train_index];

    plan.path = findPath(route, train.trajectory, train.coord, train.dir, traj, coord);

    if (!plan.path.found)
    {
        plan.error = plan.path.error;
        return plan;
    }

    plan.length = plan.path.length;

    if (plan.length < 20.0)
    {
        plan.error = QObject::tr("Начальная и конечная точки почти совпадают");
        return plan;
    }

    plan.dynamics = trainDynamics(train, catalog);

    if (plan.path.wrongWay)
    {
        plan.warnings << QObject::tr("Путь частично идёт по неправильному пути "
                                     "(против направления светофоров)");
    }

    if (plan.path.switchesCrossed > 0)
    {
        plan.warnings << QObject::tr("Стрелок на пути: %1. Их нужно перевести триггерами "
                                     "сценария (этап «Триггеры»)").arg(plan.path.switchesCrossed);
    }

    // Профиль пути
    const double step = plan.profile.step;
    const int n = static_cast<int>(std::ceil(plan.length / step)) + 1;

    plan.profile.height.resize(n);
    plan.profile.limit.resize(n);

    QVector<double> raw(n);

    for (int i = 0; i < n; ++i)
    {
        const TripPosition p = plan.position(std::min(i * step, plan.length));
        const TrajectoryData *t = route.find(p.trajectory);

        plan.profile.height[i] = (t != nullptr) ? t->heightAt(p.coord) : 0.0;
        raw[i] = (t != nullptr) ? t->speedLimitAt(p.coord) / 3.6 : 300.0 / 3.6;
    }

    // Ограничение действует, пока его не проехал хвост состава
    const int win = static_cast<int>(plan.dynamics.length / step);

    for (int i = 0; i < n; ++i)
    {
        double m = raw[i];

        for (int j = std::max(0, i - win); j < i; ++j)
        {
            m = std::min(m, raw[j]);
        }

        plan.profile.limit[i] = m;
    }

    // Станции вдоль пути
    struct Sample
    {
        double s;
        QPointF pos;
    };

    QVector<Sample> samples;

    for (double s = 0.0; s <= plan.length + 1e-6; s += STATION_STEP)
    {
        const TripPosition p = plan.position(std::min(s, plan.length));
        const TrajectoryData *t = route.find(p.trajectory);

        if (t != nullptr)
        {
            samples.push_back({s, t->pointAt(p.coord)});
        }
    }

    for (const StationData &st : route.stations())
    {
        double best = STATION_RADIUS * STATION_RADIUS;
        int bi = -1;

        for (int i = 0; i < samples.size(); ++i)
        {
            const QPointF d = samples[i].pos - st.pos;
            const double dd = d.x() * d.x() + d.y() * d.y();

            if (dd < best)
            {
                best = dd;
                bi = i;
            }
        }

        if (bi >= 0)
        {
            plan.stations.push_back({st.name, samples[bi].s, std::sqrt(best)});
        }
    }

    std::sort(plan.stations.begin(), plan.stations.end(),
              [](const TripStation &a, const TripStation &b) { return a.s < b.s; });

    // Неподвижные поезда на пути
    for (int i = 0; i < scenario.size(); ++i)
    {
        if (i == train_index)
        {
            continue;
        }

        const PlacedTrain &other = scenario.trains()[i];

        if (other.timetable.valid)
        {
            continue;
        }

        const PlacementCheck chk = scenario.check(route, other, i);

        double acc = 0.0;
        bool hit = false;

        for (const PathSegment &seg : plan.path.segments)
        {
            const double lo = std::min(seg.from, seg.to);
            const double hi = std::max(seg.from, seg.to);

            for (const FootprintSegment &fs : chk.footprint.segments)
            {
                if (fs.trajectory != seg.trajectory)
                {
                    continue;
                }

                const double a = std::max(lo, fs.from);
                const double b = std::min(hi, fs.to);

                if (b > a)
                {
                    const double at = (seg.dir > 0) ? (a - seg.from) : (seg.from - b);

                    plan.warnings << QObject::tr("Путь занят стоящим поездом «%1» (примерно %2 м от "
                                                 "начала): поезд упрётся в него")
                                     .arg(other.name).arg(acc + at, 0, 'f', 0);
                    hit = true;
                    break;
                }
            }

            if (hit)
            {
                break;
            }

            acc += seg.length();
        }
    }

    plan.ok = true;

    return plan;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
TripResult computeTrip(const TripPlan &plan, const TripOptions &opts, const Route &route,
                       const Scenario &scenario, int train_index)
{
    TripResult res;

    if (!plan.ok)
    {
        res.error = plan.error;
        return res;
    }

    const double step = plan.profile.step;
    const int nodes = plan.profile.size();

    // Строки: начало, выбранные станции, конец
    struct Row
    {
        QString name;
        double s;
        StopMode mode;
        int dwell;
        double extraMin;
    };

    QVector<Row> rows;

    auto nearest_name = [&plan](double s, const QString &fallback)
    {
        double best = 200.0;
        QString name = fallback;

        for (const TripStation &st : plan.stations)
        {
            if (std::abs(st.s - s) < best)
            {
                best = std::abs(st.s - s);
                name = st.name;
            }
        }

        return name;
    };

    rows.push_back({opts.startName.isEmpty() ? nearest_name(0.0, QObject::tr("Отправление"))
                                              : opts.startName,
                    0.0, StopMode::Stop, 0, 0.0});

    QVector<StopSpec> specs = opts.stops;

    std::sort(specs.begin(), specs.end(), [&plan](const StopSpec &a, const StopSpec &b)
    {
        return plan.stations.value(a.station).s < plan.stations.value(b.station).s;
    });

    for (const StopSpec &sp : specs)
    {
        if ((sp.mode == StopMode::Skip) || (sp.station < 0) || (sp.station >= plan.stations.size()))
        {
            continue;
        }

        const TripStation &st = plan.stations[sp.station];

        if ((st.s < 30.0) || (st.s > plan.length - 30.0))
        {
            continue;
        }

        if (!rows.isEmpty() && (st.s - rows.last().s < 30.0))
        {
            continue;
        }

        rows.push_back({st.name, st.s, sp.mode,
                        (sp.mode == StopMode::Stop) ? std::max(60, ceilMinute(sp.dwellSec)) : 0,
                        sp.extraMin});
    }

    rows.push_back({opts.endName.isEmpty() ? nearest_name(plan.length, QObject::tr("Прибытие"))
                                           : opts.endName,
                    plan.length, StopMode::Stop, 0, opts.endExtraMin});

    // Ход
    QVector<int> stops;

    for (int k = 1; k + 1 < rows.size(); ++k)
    {
        if (rows[k].mode == StopMode::Stop)
        {
            stops.push_back(static_cast<int>(std::lround(rows[k].s / step)));
        }
    }

    const RunResult run = simulateRun(plan.dynamics, plan.profile, stops);

    if (!run.ok)
    {
        res.error = run.error;
        return res;
    }

    res.warnings = run.warnings;
    res.runTime = run.timeAt(plan.length, step);

    // Запас: на весь путь пропорционально времени хода перегонов + личный на перегон
    const int cnt = rows.size();
    QVector<double> base(cnt);

    for (int k = 0; k < cnt; ++k)
    {
        base[k] = run.timeAt(rows[k].s, step);
    }

    const double total_run = std::max(1.0, base.last());

    res.rows.resize(cnt);

    double cum_extra = 0.0;
    double dwell_before = 0.0;
    int prev_departure = opts.startTime;

    for (int k = 0; k < cnt; ++k)
    {
        TripRow &row = res.rows[k];

        row.name = rows[k].name;
        row.s = rows[k].s;
        row.mode = rows[k].mode;
        row.runTime = base[k];
        row.position = plan.position(rows[k].s);

        const int ni = std::min(nodes - 1, static_cast<int>(std::lround(rows[k].s / step)));
        row.speedKmh = run.speed.value(ni) * 3.6;

        if (k == 0)
        {
            row.arrival = opts.startTime;
            row.departure = opts.startTime;
            continue;
        }

        const double leg = base[k] - base[k - 1];

        cum_extra += opts.totalExtraMin * 60.0 * leg / total_run + rows[k].extraMin * 60.0;

        const double exact = opts.startTime + base[k] + dwell_before + cum_extra;

        row.arrival = std::max(ceilMinute(exact), prev_departure + 60);
        row.reserve = static_cast<int>(std::lround(cum_extra));
        row.departure = row.arrival;

        if ((k + 1 < cnt) && (row.mode == StopMode::Stop))
        {
            row.departure = row.arrival + rows[k].dwell;
            dwell_before += rows[k].dwell;
        }

        prev_departure = row.departure;
    }

    // Расписание и точки движения для проверки пересечений
    Timetable &tt = res.timetable;

    tt.valid = true;
    tt.startTime = opts.startTime;
    tt.endTrajectory = res.rows.last().position.trajectory;
    tt.endCoord = res.rows.last().position.coord;
    tt.totalExtraMin = opts.totalExtraMin;

    for (int k = 0; k < res.rows.size(); ++k)
    {
        const TripRow &row = res.rows[k];
        TimetableStation st;

        st.extraMin = rows[k].extraMin;

        st.name = row.name;
        st.arrival = row.arrival;
        st.departure = row.departure;
        st.targetTrajectory = row.position.trajectory;
        st.targetCoord = row.position.coord;
        tt.stations.push_back(st);
    }

    auto add_sample = [&tt](int t, const TripPosition &p)
    {
        TimetableSample sm;

        sm.time = t;
        sm.trajectory = p.trajectory;
        sm.coord = p.coord;
        sm.dir = p.dir;
        tt.samples.push_back(sm);
    };

    for (int k = 0; k + 1 < cnt; ++k)
    {
        const TripRow &a = res.rows[k];
        const TripRow &b = res.rows[k + 1];

        add_sample(a.arrival, a.position);

        for (int t = a.arrival + 30; t < a.departure; t += 30)
        {
            add_sample(t, a.position);
        }

        add_sample(a.departure, a.position);

        const double r0 = base[k];
        const double r1 = base[k + 1];
        const int i0 = static_cast<int>(std::lround(rows[k].s / step));
        const int i1 = static_cast<int>(std::lround(rows[k + 1].s / step));

        for (int i = i0 + 10; i < i1; i += 10)
        {
            const double f = (r1 > r0) ? (run.time[i] - r0) / (r1 - r0) : 0.0;
            const int t = a.departure + static_cast<int>(f * (b.arrival - a.departure));

            add_sample(t, plan.position(std::min(i * step, plan.length)));
        }
    }

    add_sample(res.rows.last().arrival, res.rows.last().position);

    // Пересечения с другими поездами по расписанию
    if ((train_index >= 0) && (train_index < scenario.size()))
    {
        const double my_len = plan.dynamics.length;

        for (int i = 0; i < scenario.size(); ++i)
        {
            const PlacedTrain &other = scenario.trains()[i];

            if ((i == train_index) || !other.timetable.valid)
            {
                continue;
            }

            bool found = false;

            for (const TimetableSample &mine : tt.samples)
            {
                double lo = 0.0;
                double hi = 0.0;

                spanOf(mine.coord, mine.dir, my_len, &lo, &hi);

                for (const TimetableSample &theirs : other.timetable.samples)
                {
                    if ((theirs.trajectory != mine.trajectory)
                        || (std::abs(theirs.time - mine.time) > CONFLICT_WINDOW))
                    {
                        continue;
                    }

                    double lo2 = 0.0;
                    double hi2 = 0.0;

                    spanOf(theirs.coord, theirs.dir, other.length, &lo2, &hi2);

                    if ((lo < hi2 + CONFLICT_MARGIN) && (lo2 < hi + CONFLICT_MARGIN))
                    {
                        const QString kind = (theirs.dir != mine.dir)
                                ? QObject::tr("встречное движение")
                                : QObject::tr("слишком малый интервал между поездами");

                        res.warnings << QObject::tr("Возможный конфликт с поездом «%1» около %2 "
                                                    "(%3, путь %4): сдвиньте время отправления "
                                                    "или увеличьте запас")
                                        .arg(other.name, formatClock(mine.time), kind, mine.trajectory);
                        found = true;
                        break;
                    }
                }

                if (found)
                {
                    break;
                }
            }
        }
    }

    for (const QString &w : plan.warnings)
    {
        res.warnings << w;
    }

    Q_UNUSED(route);

    res.ok = true;

    return res;
}

} // namespace rse
