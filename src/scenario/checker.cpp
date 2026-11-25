#include    "scenario/checker.h"
#include    "scenario/autotriggers.h"

#include    "scenario/footprint.h"
#include    "scenario/formation.h"

#include    <QMap>
#include    <QObject>

#include    <algorithm>
#include    <limits>

namespace rse
{

namespace
{

constexpr int FAR_FUTURE = std::numeric_limits<int>::max() / 2;

//------------------------------------------------------------------------------
// Поезд на временной сетке
//------------------------------------------------------------------------------
struct Actor
{
    int index = -1;
    const PlacedTrain *train = nullptr;

    /// Когда поезд появляется / исчезает (сцепка), с
    int from = 0;
    int to = FAR_FUTURE;

    /// Движение по расписанию: [t0, t1]
    bool moves = false;
    int t0 = 0;
    int t1 = 0;

    Footprint placed;
    Footprint parked;
    Footprint parkedExt;
    Footprint placedExt;
    int parkedDir = 1;
};

//------------------------------------------------------------------------------
// Накопитель: одна проблема на (пара, вид), время расширяется
//------------------------------------------------------------------------------
struct Hit
{
    int first = 0;
    int last = 0;
    QString trajectory;
};

QString headTrajectory(const Footprint &fp)
{
    return fp.segments.isEmpty() ? QString() : fp.segments.first().trajectory;
}

} // namespace

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
QString clockText(int seconds)
{
    seconds = ((seconds % 86400) + 86400) % 86400;

    return QStringLiteral("%1:%2:%3")
        .arg(seconds / 3600, 2, 10, QLatin1Char('0'))
        .arg(seconds / 60 % 60, 2, 10, QLatin1Char('0'))
        .arg(seconds % 60, 2, 10, QLatin1Char('0'));
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
QVector<ScheduleIssue> checkSchedules(const Route &route, const Scenario &scenario,
                                      const CheckOptions &options)
{
    QVector<ScheduleIssue> issues;

    auto add = [&](IssueSeverity sev, const QString &kind, const QStringList &trains,
                   int time, int time_end, const QString &traj, const QString &text)
    {
        ScheduleIssue is;

        is.severity = sev;
        is.kind = kind;
        is.trains = trains;
        is.time = time;
        is.timeEnd = time_end;
        is.trajectory = traj;
        is.text = text;
        issues.push_back(is);
    };

    const int count = scenario.size();

    //--------------------------------------------------------------------------
    // 1. Ошибки в самих расписаниях
    //--------------------------------------------------------------------------
    for (const PlacedTrain &t : scenario.trains())
    {
        const Timetable &tt = t.timetable;

        if (!tt.valid)
        {
            continue;
        }

        if (!t.autoMode && !t.isVirtual())
        {
            add(IssueSeverity::Warning, "no-bot", { t.name }, -1, -1, QString(),
                QObject::tr("У поезда «%1» есть график, но не включён бот: он будет стоять. "
                            "Включите «Бот» в положении поезда.").arg(t.name));
        }

        if (!tt.stations.isEmpty() && tt.startTime < options.startTime)
        {
            add(IssueSeverity::Error, "before-start", { t.name }, tt.startTime, -1, QString(),
                QObject::tr("Поезд «%1» отправляется в %2, раньше начала сценария (%3).")
                    .arg(t.name, clockText(tt.startTime), clockText(options.startTime)));
        }

        int prev = -1;

        for (int k = 0; k < tt.stations.size(); ++k)
        {
            const TimetableStation &st = tt.stations[k];

            if (k > 0 && st.arrival < prev)
            {
                add(IssueSeverity::Error, "order", { t.name }, st.arrival, -1, QString(),
                    QObject::tr("Поезд «%1»: прибытие на «%2» (%3) раньше отправления с "
                                "предыдущей станции (%4).")
                        .arg(t.name, st.name, clockText(st.arrival), clockText(prev)));
            }

            if (st.departure < st.arrival)
            {
                add(IssueSeverity::Error, "order", { t.name }, st.arrival, -1, QString(),
                    QObject::tr("Поезд «%1»: на «%2» отправление раньше прибытия.")
                        .arg(t.name, st.name));
            }

            prev = st.departure;
        }

        if (tt.samples.isEmpty())
        {
            add(IssueSeverity::Info, "no-samples", { t.name }, -1, -1, QString(),
                QObject::tr("График поезда «%1» загружен из файла и не имеет расчёта движения: "
                            "пересечения с ним не проверяются. Пересчитайте график в редакторе.")
                    .arg(t.name));
        }
    }

    //--------------------------------------------------------------------------
    // 2. Продолжения: сцепка и время
    //--------------------------------------------------------------------------
    QMap<QString, int> ends_at;      // имя -> когда поезд исчезает (сцепился)
    QMap<int, FormationPlan> forms;  // индекс продолжения -> расчёт

    for (int i = 0; i < count; ++i)
    {
        const PlacedTrain &c = scenario.trains()[i];

        if (!c.isContinuation())
        {
            continue;
        }

        const FormationPlan plan = planFormation(route, scenario, c);

        if (!plan.ok)
        {
            add(IssueSeverity::Error, "formation", { c.name }, -1, -1, QString(), plan.error);
            continue;
        }

        forms.insert(i, plan);

        for (const QString &w : plan.warnings)
        {
            add(IssueSeverity::Error, "formation", { c.name, c.formedFrom }, plan.arrivalTime,
                -1, plan.locoAtEnd.trajectory, w);
        }

        if (!c.timetable.valid)
        {
            add(IssueSeverity::Warning, "formation", { c.name }, plan.readyTime, -1, QString(),
                QObject::tr("У поезда после сцепки «%1» нет графика: после сцепки поезд будет стоять.")
                    .arg(c.name));
        }
        else if (c.timetable.startTime < plan.readyTime)
        {
            add(IssueSeverity::Error, "formation", { c.name, c.formedFrom }, c.timetable.startTime,
                plan.readyTime, QString(),
                QObject::tr("Поезд «%1» отправляется в %2, но локомотив «%3» прибудет в %4, "
                            "а сцепка и переименование - не раньше %5.")
                    .arg(c.name, clockText(c.timetable.startTime), c.formedFrom,
                         clockText(plan.arrivalTime), clockText(plan.readyTime)));
        }

        const int end = c.timetable.valid ? c.timetable.startTime : plan.readyTime;

        ends_at.insert(c.formedFrom, end);
        ends_at.insert(c.formedWagons, end);
    }

    //--------------------------------------------------------------------------
    // 3. Участники временной сетки
    //--------------------------------------------------------------------------
    QVector<Actor> actors;
    int grid_from = FAR_FUTURE;
    int grid_to = 0;

    for (int i = 0; i < count; ++i)
    {
        const PlacedTrain &t = scenario.trains()[i];
        Actor a;

        a.index = i;
        a.train = &t;

        if (t.planned)
        {
            // Будущий поезд без привязки: когда и где он появится, неизвестно
            continue;
        }

        if (t.isContinuation())
        {
            if (!forms.contains(i) || !t.timetable.valid)
            {
                continue;
            }

            a.from = t.timetable.startTime;
        }

        if (ends_at.contains(t.name))
        {
            a.to = ends_at.value(t.name);
        }

        const Timetable &tt = t.timetable;

        // положение до начала движения
        const PlacedTrain &base = t.isContinuation() ? forms[i].formed : t;

        a.placed = computeFootprint(route, base.trajectory, base.coord, base.dir, base.length);
        a.placedExt = extendedFootprint(route, a.placed, base.dir, Scenario::COUPLE_DISTANCE);

        a.parked = a.placed;
        a.parkedExt = a.placedExt;
        a.parkedDir = base.dir;

        if (tt.valid && !tt.samples.isEmpty())
        {
            a.moves = true;
            a.t0 = tt.samples.first().time;
            a.t1 = tt.samples.last().time;

            const TimetableSample &last = tt.samples.last();

            a.parked = computeFootprint(route, last.trajectory, last.coord, last.dir, base.length);
            a.parkedExt = extendedFootprint(route, a.parked, last.dir, Scenario::COUPLE_DISTANCE);
            a.parkedDir = last.dir;

            grid_from = std::min(grid_from, std::max(a.t0, a.from));
            grid_to = std::max(grid_to, std::min(a.t1, a.to));
        }

        actors.push_back(a);
    }

    if (grid_from > grid_to)
    {
        //--------------------------------------------------------------------------
    // 4. Маршруты, которые редактор строит триггерами, занимают пути: очерёдность по
    //    категории проблем не создаёт, споры поездов одной категории - создают
    //--------------------------------------------------------------------------
    {
        const AutoTriggerPlan at = planAutoTriggers(route, scenario);

        for (const RouteConflict &c : routeConflicts(at, scenario))
        {
            add(IssueSeverity::Warning, QStringLiteral("route"), { c.trainA, c.trainB }, c.t0, c.t1,
                c.trajectory,
                QObject::tr("Маршруты поездов «%1» и «%2» одновременно занимают «%3» (%4–%5): "
                            "один из поездов будет ждать светофор.")
                    .arg(c.trainA, c.trainB, c.trajectory, clockText(c.t0), clockText(c.t1)));
        }
    }

    std::sort(issues.begin(), issues.end(), [](const ScheduleIssue &l, const ScheduleIssue &r)
        {
            return l.severity < r.severity;
        });

        return issues;
    }

    //--------------------------------------------------------------------------
    // 4. Движение по сетке времени
    //--------------------------------------------------------------------------
    const int end_time = grid_to + 120;
    int step = std::max(options.stepSec, 1);

    while ((end_time - grid_from) / step > 6000)
    {
        step *= 2;
    }

    QMap<QString, Hit> hits;

    auto movingAt = [](const Actor &a, int t)
    {
        return a.moves && t > a.t0 && t < a.t1;
    };

    auto isFormationPair = [&](const Actor &a, const Actor &b)
    {
        for (const FormationPlan &p : forms)
        {
            if ((p.locoIndex == a.index && p.wagonsIndex == b.index)
                || (p.locoIndex == b.index && p.wagonsIndex == a.index))
            {
                return true;
            }
        }

        return false;
    };

    struct Now
    {
        bool active = false;
        bool moving = false;
        Footprint fp;
        Footprint ext;
        int dir = 1;
        QString head;
    };

    QVector<Now> now(actors.size());

    for (int t = grid_from; t <= end_time; t += step)
    {
        for (int k = 0; k < actors.size(); ++k)
        {
            const Actor &a = actors[k];
            Now &n = now[k];

            n.active = (t >= a.from) && (t < a.to);
            n.moving = false;

            if (!n.active)
            {
                continue;
            }

            if (!a.moves || t <= a.t0)
            {
                n.fp = a.placed;
                n.ext = a.placedExt;
                n.dir = a.train->isContinuation() ? forms[a.index].formed.dir : a.train->dir;
            }
            else if (t >= a.t1)
            {
                n.fp = a.parked;
                n.ext = a.parkedExt;
                n.dir = a.parkedDir;
            }
            else
            {
                // между точками: положение по предыдущей точке, на той же траектории -
                // линейная интерполяция координаты
                const QVector<TimetableSample> &sm = a.train->timetable.samples;
                int lo = 0;
                int hi = sm.size() - 1;

                while (hi - lo > 1)
                {
                    const int mid = (lo + hi) / 2;

                    if (sm[mid].time <= t)
                    {
                        lo = mid;
                    }
                    else
                    {
                        hi = mid;
                    }
                }

                const TimetableSample &p = sm[lo];
                const TimetableSample &q = sm[hi];
                double coord = p.coord;

                if (p.trajectory == q.trajectory && q.time > p.time)
                {
                    const double f = double(t - p.time) / double(q.time - p.time);
                    coord = p.coord + (q.coord - p.coord) * std::clamp(f, 0.0, 1.0);
                }

                const double len = a.train->isContinuation() ? forms[a.index].formed.length
                                                             : a.train->length;

                n.fp = computeFootprint(route, p.trajectory, coord, p.dir, len);
                n.dir = p.dir;
                n.moving = true;
                n.ext = extendedFootprint(route, n.fp, p.dir, options.headwayMargin);
            }

            n.head = headTrajectory(n.fp);
        }

        for (int i = 0; i < actors.size(); ++i)
        {
            for (int j = i + 1; j < actors.size(); ++j)
            {
                const Now &a = now[i];
                const Now &b = now[j];

                if (!a.active || !b.active)
                {
                    continue;
                }

                const QString pair = actors[i].train->name + QLatin1Char('\n') + actors[j].train->name;

                const double ov = overlapLength(a.fp, b.fp);

                QString kind;

                if (ov > 0.5)
                {
                    kind = (a.moving && b.moving && a.head == b.head && a.dir != b.dir)
                               ? QStringLiteral("head-on") : QStringLiteral("collision");
                }
                else if ((actors[i].moves || actors[j].moves) && !isFormationPair(actors[i], actors[j]))
                {
                    if (a.moving && b.moving)
                    {
                        if (overlapLength(a.ext, b.fp) > 0.0 || overlapLength(b.ext, a.fp) > 0.0)
                        {
                            kind = QStringLiteral("headway");
                        }
                    }
                    else
                    {
                        // хотя бы один стоит: ближе зоны автосцепки - сцепятся
                        Footprint za = a.ext;
                        Footprint zb = b.ext;

                        if (a.moving)
                        {
                            za = extendedFootprint(route, a.fp, a.dir, Scenario::COUPLE_DISTANCE);
                        }

                        if (b.moving)
                        {
                            zb = extendedFootprint(route, b.fp, b.dir, Scenario::COUPLE_DISTANCE);
                        }

                        if (overlapLength(za, b.fp) > 0.0 || overlapLength(zb, a.fp) > 0.0)
                        {
                            kind = QStringLiteral("couple");
                        }
                    }
                }

                if (kind.isEmpty())
                {
                    continue;
                }

                const QString key = pair + QLatin1Char('\n') + kind;

                if (!hits.contains(key))
                {
                    Hit h;
                    h.first = t;
                    h.last = t;
                    h.trajectory = a.moving ? a.head : b.head;
                    hits.insert(key, h);
                }
                else
                {
                    hits[key].last = t;
                }
            }
        }
    }

    for (auto it = hits.constBegin(); it != hits.constEnd(); ++it)
    {
        const QStringList parts = it.key().split(QLatin1Char('\n'));
        const QString &a = parts[0];
        const QString &b = parts[1];
        const QString &kind = parts[2];
        const Hit &h = it.value();

        const bool to_end = h.last >= end_time - step;
        const QString when = to_end ? QObject::tr("с %1 и далее").arg(clockText(h.first))
                                    : QObject::tr("с %1 по %2").arg(clockText(h.first), clockText(h.last));

        if (kind == QLatin1String("collision"))
        {
            add(IssueSeverity::Error, kind, { a, b }, h.first, to_end ? -1 : h.last, h.trajectory,
                QObject::tr("Поезда «%1» и «%2» оказываются на одном месте пути (%3, путь %4): "
                            "один из них прибывает туда, где стоит или проезжает другой.")
                    .arg(a, b, when, h.trajectory));
        }
        else if (kind == QLatin1String("head-on"))
        {
            add(IssueSeverity::Error, kind, { a, b }, h.first, to_end ? -1 : h.last, h.trajectory,
                QObject::tr("Встречное движение поездов «%1» и «%2» на одном пути (%3, путь %4): "
                            "разведите их по времени или по путям станции.")
                    .arg(a, b, when, h.trajectory));
        }
        else if (kind == QLatin1String("headway"))
        {
            add(IssueSeverity::Warning, kind, { a, b }, h.first, to_end ? -1 : h.last, h.trajectory,
                QObject::tr("Поезда «%1» и «%2» ближе %3 м друг к другу (%4, около пути %5): "
                            "возможен затор, второй поезд остановится на светофоре. "
                            "Увеличьте интервал отправления.")
                    .arg(a, b).arg(options.headwayMargin, 0, 'f', 0).arg(when, h.trajectory));
        }
        else
        {
            add(IssueSeverity::Warning, kind, { a, b }, h.first, to_end ? -1 : h.last, h.trajectory,
                QObject::tr("Поезд подходит к стоящему поезду ближе %1 м (%2 и %3, %4, путь %5): "
                            "в игре они автоматически сцепятся. Если это не задумано - "
                            "остановите состав дальше.")
                    .arg(Scenario::COUPLE_DISTANCE, 0, 'f', 0).arg(a, b, when, h.trajectory));
        }
    }

    std::stable_sort(issues.begin(), issues.end(), [](const ScheduleIssue &l, const ScheduleIssue &r)
    {
        if (l.severity != r.severity)
        {
            return l.severity < r.severity;
        }

        const int lt = (l.time < 0) ? FAR_FUTURE : l.time;
        const int rt = (r.time < 0) ? FAR_FUTURE : r.time;

        return lt < rt;
    });

    return issues;
}

} // namespace rse
