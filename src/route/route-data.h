#ifndef     RSE_ROUTE_DATA_H
#define     RSE_ROUTE_DATA_H

#include    <QString>
#include    <QVector>
#include    <QPointF>
#include    <QRectF>

namespace rse
{

//------------------------------------------------------------------------------
// Ограничение скорости на участке траектории [from, to], м, км/ч
//------------------------------------------------------------------------------
struct SpeedLimitInterval
{
    double from = 0.0;
    double to = 0.0;
    double kmh = 300.0;
};

//------------------------------------------------------------------------------
// Траектория: непрерывный участок пути между стрелками (файл *.traj)
//------------------------------------------------------------------------------
struct TrajectoryData
{
    /// Имя траектории (имя файла без расширения)
    QString name;

    /// Точки полилинии в плане (x, y), м
    QVector<QPointF> points;

    /// Накопленная длина до каждой точки, м (считается в 3D, как в симуляторе)
    QVector<double> cumLength;

    /// Железнодорожный пикетаж в каждой точке
    QVector<double> railwayCoord;

    /// Высота (z) в каждой точке, м
    QVector<double> heights;

    /// Ограничения скорости на траектории (из trajectory-speedmap), км/ч
    QVector<SpeedLimitInterval> speedLimits;

    /// Полная длина траектории, м
    double length = 0.0;

    /// Габарит траектории в плане
    QRectF bounds;

    bool isValid() const
    {
        return points.size() >= 2;
    }

    /// Точка плана на траекторной координате (координата ограничивается
    /// диапазоном [0, length]). В tangent возвращается единичный вектор
    /// направления движения от начала к концу траектории
    QPointF pointAt(double coord, QPointF *tangent = nullptr) const;

    /// Высота пути на координате (линейно между точками)
    double heightAt(double coord) const;

    /// Допустимая скорость на координате, км/ч (300 - ограничений нет). Если
    /// ограничения перекрываются, действует меньшее
    double speedLimitAt(double coord) const;

    /// Полилиния участка [from, to] (в плане), точки в порядке возрастания координаты
    QVector<QPointF> subPolyline(double from, double to) const;
};

//------------------------------------------------------------------------------
// Стрелка (элемент Switch в topology.xml). Служит узлом соединения траекторий.
//------------------------------------------------------------------------------
struct SwitchData
{
    QString name;

    /// Траектории со стороны "назад": своим КОНЦОМ примыкают к стрелке
    QString bwdPlus;
    QString bwdMinus;

    /// Траектории со стороны "вперёд": своим НАЧАЛОМ примыкают к стрелке
    QString fwdPlus;
    QString fwdMinus;

    /// Траектория со стороны "вперёд", примыкающая к стрелке КОНЦОМ
    /// (элемент fwdMinusTrajReversed)
    QString fwdMinusReversed;

    /// Литеры светофоров (если есть)
    QString signalFwd;
    QString signalBwd;

    /// Положение узла в плане (вычисляется по примыкающим траекториям)
    QPointF pos;
    bool hasPos = false;
};

//------------------------------------------------------------------------------
// Станция (stations.conf) - подпись на карте
//------------------------------------------------------------------------------
struct StationData
{
    QString name;
    QPointF pos;
};

//------------------------------------------------------------------------------
// Путевая точка (waypoints.conf) - готовое место старта поезда
//------------------------------------------------------------------------------
struct WaypointData
{
    QString title;
    QString trajectory;
    int dir = 1;
    double coord = 0.0;
};

} // namespace rse

#endif // RSE_ROUTE_DATA_H
