#include    "scenario/scenario.h"

#include    <algorithm>

namespace rse
{

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
int Scenario::add(const PlacedTrain &train)
{
    trains_.push_back(train);
    return trains_.size() - 1;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void Scenario::replace(int index, const PlacedTrain &train)
{
    if (index >= 0 && index < trains_.size())
    {
        trains_[index] = train;
    }
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void Scenario::remove(int index)
{
    if (index >= 0 && index < trains_.size())
    {
        trains_.remove(index);
    }
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void Scenario::clear()
{
    trains_.clear();
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
int Scenario::indexOfName(const QString &name) const
{
    for (int i = 0; i < trains_.size(); ++i)
    {
        if (trains_[i].name == name)
        {
            return i;
        }
    }

    return -1;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
QString Scenario::uniqueName(const QString &base, int ignore_index) const
{
    auto taken = [&](const QString &n)
    {
        const int i = indexOfName(n);
        return i >= 0 && i != ignore_index;
    };

    int n = 1;

    while (taken(QStringLiteral("%1 %2").arg(base).arg(n)))
    {
        ++n;
    }

    return QStringLiteral("%1 %2").arg(base).arg(n);
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
PlacementCheck Scenario::check(const Route &route, const PlacedTrain &train,
                               int ignore_index) const
{
    PlacementCheck res;

    res.footprint = computeFootprint(route, train.trajectory, train.coord,
                                     train.dir, train.length);

    const Footprint zone = extendedFootprint(route, res.footprint, train.dir,
                                             COUPLE_DISTANCE);

    for (int i = 0; i < trains_.size(); ++i)
    {
        // Продолжение на путях не стоит
        if (i == ignore_index || trains_[i].isVirtual())
        {
            continue;
        }

        const PlacedTrain &other = trains_[i];

        const Footprint ofp = computeFootprint(route, other.trajectory, other.coord,
                                               other.dir, other.length);

        const double ov = overlapLength(res.footprint, ofp);

        if (ov > 0.0)
        {
            res.overlap += ov;
            res.overlapWith << other.name;
        }
        else if (overlapLength(zone, ofp) > 0.0)
        {
            res.nearTrains << other.name;
        }
    }

    return res;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
PlacedTrain Scenario::withFlippedHead(const PlacedTrain &train)
{
    PlacedTrain t = train;
    t.dir = -train.dir;
    t.timetable = Timetable();

    return t;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
PlacedTrain Scenario::reversedInPlace(const Route &route, const PlacedTrain &train)
{
    const Footprint fp = computeFootprint(route, train.trajectory, train.coord,
                                          train.dir, train.length);

    PlacedTrain t = train;
    t.timetable = Timetable();

    if (!fp.isEmpty() && !fp.tailTrajectory.isEmpty())
    {
        t.trajectory = fp.tailTrajectory;
        t.coord = fp.tailCoord;
        t.dir = fp.tailMove;
    }

    std::reverse(t.groups.begin(), t.groups.end());

    for (ConsistGroup &g : t.groups)
    {
        g.forward = !g.forward;
    }

    return t;
}

} // namespace rse
