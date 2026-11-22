#ifndef     RSE_FOOTPRINT_H
#define     RSE_FOOTPRINT_H

#include    "route/route.h"

#include    <QString>
#include    <QVector>

namespace rse
{

//------------------------------------------------------------------------------
// Участок траектории [from, to], занятый составом (from <= to)
//------------------------------------------------------------------------------
struct FootprintSegment
{
    QString trajectory;
    double from = 0.0;
    double to = 0.0;
};

//------------------------------------------------------------------------------
// Где на путях стоит состав. Голова в точке (trajectory, coord), dir - куда
// смотрит голова (+1 к концу траектории, -1 к началу); вагоны идут назад от
// головы через стрелки, как это делает симулятор при расстановке поезда
//------------------------------------------------------------------------------
struct Footprint
{
    /// Участки от головы к хвосту
    QVector<FootprintSegment> segments;

    /// Длина состава, м
    double length = 0.0;

    /// Хвост упёрся в тупик, не уместившись на путях; сколько метров не хватило
    bool deadEnd = false;
    double overhang = 0.0;

    /// Путь проходит через стрелку, положение которой по данным однозначно не определить
    bool ambiguous = false;

    /// Положение хвоста (последняя точка следа)
    QString tailTrajectory;
    double tailCoord = 0.0;

    /// Куда двигаться от хвоста дальше «назад» (направление в координатах хвостовой траектории)
    int tailMove = 1;

    bool isEmpty() const
    {
        return segments.isEmpty();
    }
};

/// Пройти от точки на расстояние length в направлении move (+1 к концу, -1 к началу)
Footprint walkTrack(const Route &route, const QString &trajectory, double coord,
                    int move, double length);

/// Положение состава длиной length с головой в (trajectory, coord), смотрящей в dir
Footprint computeFootprint(const Route &route, const QString &trajectory, double coord,
                           int dir, double length);

/// Общая длина пересечения участков двух следов, м
double overlapLength(const Footprint &a, const Footprint &b);

/// Следы, расширенные с обоих концов на margin метров (зона сцепки)
Footprint extendedFootprint(const Route &route, const Footprint &fp, int dir, double margin);

} // namespace rse

#endif // RSE_FOOTPRINT_H
