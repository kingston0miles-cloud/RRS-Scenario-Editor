#include    "scenario/footprint.h"

#include    <algorithm>

namespace rse
{

namespace
{

/// Предел переходов через стрелки при обходе (защита от кольцевых маршрутов)
constexpr int HOP_LIMIT = 500;

/// Пересечение короче этого не считается (стыки траекторий)
constexpr double OVERLAP_EPS = 0.01;

} // namespace

//------------------------------------------------------------------------------
// Обход пути: если участок кончился, переходим через стрелку на следующую
// траекторию. Вход в неё с начала - дальше едем к её концу, с конца - к началу
//------------------------------------------------------------------------------
Footprint walkTrack(const Route &route, const QString &trajectory, double coord,
                    int move, double length)
{
    Footprint fp;
    fp.length = length;

    const TrajectoryData *traj = route.find(trajectory);

    if (traj == nullptr)
    {
        fp.deadEnd = true;
        fp.overhang = length;
        return fp;
    }

    QString name = trajectory;
    double c = std::clamp(coord, 0.0, traj->length);
    int m = move >= 0 ? 1 : -1;
    double remaining = length;

    for (int hop = 0; hop < HOP_LIMIT; ++hop)
    {
        traj = route.find(name);

        const double available = (m > 0) ? (traj->length - c) : c;
        const double take = std::min(remaining, available);

        FootprintSegment seg;
        seg.trajectory = name;
        seg.from = (m > 0) ? c : c - take;
        seg.to = (m > 0) ? c + take : c;
        fp.segments.push_back(seg);

        remaining -= take;
        c = (m > 0) ? c + take : c - take;

        if (remaining <= 1e-9)
        {
            fp.tailTrajectory = name;
            fp.tailCoord = c;
            fp.tailMove = m;
            return fp;
        }

        // Участок кончился: ищем продолжение за концом, куда едем
        const QVector<Route::Endpoint> next = route.crossings(name, m > 0);

        if (next.isEmpty())
        {
            fp.deadEnd = true;
            fp.overhang = remaining;
            fp.tailTrajectory = name;
            fp.tailCoord = c;
            fp.tailMove = m;
            return fp;
        }

        if (next.size() > 1)
        {
            fp.ambiguous = true;
        }

        const Route::Endpoint &e = next.first();
        const TrajectoryData *nt = route.find(e.trajectory);

        name = e.trajectory;
        m = e.atEnd ? -1 : 1;
        c = e.atEnd ? nt->length : 0.0;
    }

    // Слишком много переходов - считаем состав не уместившимся
    fp.deadEnd = true;
    fp.overhang = remaining;
    return fp;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
Footprint computeFootprint(const Route &route, const QString &trajectory, double coord,
                           int dir, double length)
{
    return walkTrack(route, trajectory, coord, dir >= 0 ? -1 : 1, length);
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
double overlapLength(const Footprint &a, const Footprint &b)
{
    double total = 0.0;

    for (const FootprintSegment &sa : a.segments)
    {
        for (const FootprintSegment &sb : b.segments)
        {
            if (sa.trajectory != sb.trajectory)
            {
                continue;
            }

            const double len = std::min(sa.to, sb.to) - std::max(sa.from, sb.from);

            if (len > OVERLAP_EPS)
            {
                total += len;
            }
        }
    }

    return total;
}

//------------------------------------------------------------------------------
// След вместе с зонами по margin метров впереди головы и позади хвоста
//------------------------------------------------------------------------------
Footprint extendedFootprint(const Route &route, const Footprint &fp, int dir, double margin)
{
    Footprint out = fp;

    if (fp.isEmpty() || margin <= 0.0)
    {
        return out;
    }

    // Впереди головы: от головы в сторону dir
    const FootprintSegment &head = fp.segments.first();
    const double head_coord = (dir >= 0) ? head.to : head.from;

    const Footprint ahead = walkTrack(route, head.trajectory, head_coord,
                                      dir >= 0 ? 1 : -1, margin);

    // Позади хвоста: от хвоста дальше в том же направлении обхода
    const Footprint behind = walkTrack(route, fp.tailTrajectory, fp.tailCoord,
                                       fp.tailMove, margin);

    out.segments += ahead.segments;
    out.segments += behind.segments;

    return out;
}

} // namespace rse
