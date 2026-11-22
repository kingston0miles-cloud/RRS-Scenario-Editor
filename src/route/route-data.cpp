#include    "route/route-data.h"

#include    <algorithm>
#include    <cmath>

namespace rse
{

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
QPointF TrajectoryData::pointAt(double coord, QPointF *tangent) const
{
    if (!isValid())
    {
        if (tangent != nullptr)
        {
            *tangent = QPointF(1.0, 0.0);
        }

        return points.isEmpty() ? QPointF() : points.first();
    }

    coord = std::clamp(coord, 0.0, length);

    // Первый элемент, строго больший coord, - конец нужного отрезка
    auto it = std::upper_bound(cumLength.begin(), cumLength.end(), coord);

    int idx = static_cast<int>(it - cumLength.begin()) - 1;
    idx = std::clamp(idx, 0, static_cast<int>(points.size()) - 2);

    const double seg_len = cumLength[idx + 1] - cumLength[idx];
    const double t = (seg_len > 0.0) ? (coord - cumLength[idx]) / seg_len : 0.0;

    const QPointF a = points[idx];
    const QPointF b = points[idx + 1];

    if (tangent != nullptr)
    {
        const QPointF d = b - a;
        const double len = std::hypot(d.x(), d.y());

        // Отрезок может быть чисто вертикальным (меняется только высота)
        *tangent = (len > 0.0) ? QPointF(d.x() / len, d.y() / len)
                               : QPointF(1.0, 0.0);
    }

    return a + (b - a) * t;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
double TrajectoryData::heightAt(double coord) const
{
    if (heights.size() != points.size() || heights.isEmpty())
    {
        return 0.0;
    }

    coord = std::clamp(coord, 0.0, length);

    auto it = std::upper_bound(cumLength.begin(), cumLength.end(), coord);

    int idx = static_cast<int>(it - cumLength.begin()) - 1;
    idx = std::clamp(idx, 0, static_cast<int>(points.size()) - 2);

    const double seg = cumLength[idx + 1] - cumLength[idx];
    const double t = (seg > 0.0) ? (coord - cumLength[idx]) / seg : 0.0;

    return heights[idx] + (heights[idx + 1] - heights[idx]) * t;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
double TrajectoryData::speedLimitAt(double coord) const
{
    double limit = 300.0;

    for (const SpeedLimitInterval &s : speedLimits)
    {
        if (coord >= s.from && coord <= s.to)
        {
            limit = std::min(limit, s.kmh);
        }
    }

    return limit;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
QVector<QPointF> TrajectoryData::subPolyline(double from, double to) const
{
    QVector<QPointF> out;

    if (!isValid())
    {
        return out;
    }

    from = std::clamp(from, 0.0, length);
    to = std::clamp(to, 0.0, length);

    if (to < from)
    {
        std::swap(from, to);
    }

    out << pointAt(from);

    for (int i = 0; i < points.size(); ++i)
    {
        if (cumLength[i] > from && cumLength[i] < to)
        {
            out << points[i];
        }
    }

    out << pointAt(to);

    return out;
}

} // namespace rse
