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

    /// Траектории, пройденные против правильного направления, без повторов -
    /// для предупреждения со ссылками на карту
    QStringList wrongTrajectories;

    /// Пройдена стрелка, положение которой по данным однозначно не определить
    /// (поезд пройдёт там, куда стрелка будет переведена маршрутом)
    int switchesCrossed = 0;

    /// Стрелки по ходу пути, которые нужно перевести (имя стрелки)
    QStringList switchNames;
};

//------------------------------------------------------------------------------
// Поездной маршрут так, как его ищет симулятор (Topology::find_route): поиск в
// ширину по траекториям и направлениям, побеждает путь с наименьшим числом
// траекторий, а не наименьшей длины. Именно так строят маршрут автопилот
// (подход -> путь станции, путь станции -> уход) и команда buildTrainRoute
//------------------------------------------------------------------------------
struct GameRouteStep
{
    QString trajectory;

    /// Направление движения по траектории (+1 к концу, -1 к началу)
    int dir = 1;

    /// Стрелка, через которую вошли на траекторию (пусто для первой), и её положение
    QString viaSwitch;
    bool viaPlus = true;
};

struct GameRoute
{
    bool found = false;
    QVector<GameRouteStep> steps;

    int hops() const
    {
        return steps.size();
    }
};

/// Маршрут от траектории start (поезд идёт по ней в направлении dir) до траектории target
GameRoute gameRoute(const Route &route, const QString &start, const QString &target, int dir);

/// Поиск кратчайшего пути для поезда, стоящего головой в (traj0, coord0) и
/// идущего в направлении dir0, до точки (traj1, coord1). Поезд не разворачивается.
/// Движение против правильного направления пути разрешено, но "стоит" wrong_way_penalty
/// метров за каждую траекторию, чтобы маршрут предпочитал правильный путь
PathResult findPath(const Route &route, const QString &traj0, double coord0, int dir0,
                    const QString &traj1, double coord1, double wrong_way_penalty = 3000.0);

} // namespace rse

#endif // RSE_PATHFINDER_H
