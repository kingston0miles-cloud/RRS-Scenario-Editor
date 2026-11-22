#ifndef     RSE_PATHFINDER_H
#define     RSE_PATHFINDER_H

#include    "route/route.h"

#include    <QString>
#include    <QStringList>
#include    <QVector>

namespace rse
{

//------------------------------------------------------------------------------
// Участок пути поезда: от координаты from до to на траектории, движение в
// направлении dir (+1 к концу траектории, -1 к началу)
//------------------------------------------------------------------------------
struct PathSegment
{
    QString trajectory;
    double from = 0.0;
    double to = 0.0;
    int dir = 1;

    double length() const
    {
        return (to > from) ? (to - from) : (from - to);
    }
};

//------------------------------------------------------------------------------
// Результат поиска пути
//------------------------------------------------------------------------------
struct PathResult
{
    bool found = false;

    /// Причина, если путь не найден
    QString error;

    QVector<PathSegment> segments;

    /// Общая длина, м
    double length = 0.0;

    /// Путь идёт хотя бы по одной траектории против её правильного направления
    bool wrongWay = false;

    /// Пройдена стрелка, положение которой по данным однозначно не определить
    /// (поезд пройдёт там, куда стрелка будет переведена маршрутом)
    int switchesCrossed = 0;

    /// Стрелки по ходу пути, которые нужно перевести (имя стрелки)
    QStringList switchNames;
};

/// Поиск кратчайшего пути для поезда, стоящего головой в (traj0, coord0) и
/// идущего в направлении dir0, до точки (traj1, coord1). Поезд не разворачивается.
/// Движение против правильного направления пути разрешено, но "стоит" wrong_way_penalty
/// метров за каждую траекторию, чтобы маршрут предпочитал правильный путь
PathResult findPath(const Route &route, const QString &traj0, double coord0, int dir0,
                    const QString &traj1, double coord1, double wrong_way_penalty = 3000.0);

} // namespace rse

#endif // RSE_PATHFINDER_H
