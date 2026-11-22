#include    "scenario/consist.h"

#include    <QObject>

#include    <algorithm>

namespace rse
{

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
QVector<ConsistGroup> flattenGroups(const QVector<ConsistGroup> &groups)
{
    QVector<ConsistGroup> units;

    for (const ConsistGroup &g : groups)
    {
        ConsistGroup u = g;
        u.count = 1;

        for (int i = 0; i < g.count; ++i)
        {
            units.push_back(u);
        }
    }

    return units;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
QVector<ConsistGroup> compressUnits(const QVector<ConsistGroup> &units)
{
    QVector<ConsistGroup> groups;

    for (const ConsistGroup &u : units)
    {
        if (!groups.isEmpty())
        {
            ConsistGroup &last = groups.last();

            if (last.configId() == u.configId() && last.module == u.module
                && last.forward == u.forward && last.brakeShoes == u.brakeShoes
                && qFuzzyCompare(last.payloadCoeff + 1.0, u.payloadCoeff + 1.0))
            {
                last.count += 1;
                continue;
            }
        }

        ConsistGroup g = u;
        g.count = 1;
        groups.push_back(g);
    }

    return groups;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void updateSummary(PlacedTrain &train, const VehicleCatalog &catalog)
{
    const TrainSummary s = catalog.summarize(train.groups);

    train.length = s.length;
    train.mass = s.mass;
    train.autopilotCapable = s.autopilotCapable;

    if (!train.autopilotCapable)
    {
        train.autoMode = false;
    }
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
EditPlan planEdit(const Scenario &scenario, const Route &route, const VehicleCatalog &catalog,
                  int index, const PlacedTrain &edited, const QVector<int> &cuts)
{
    EditPlan plan;

    const QVector<ConsistGroup> units = flattenGroups(edited.groups);

    if (units.isEmpty())
    {
        plan.errors << QObject::tr("В составе нет ни одной единицы.");
        return plan;
    }

    // Границы частей: [0, c1), [c1, c2), ..., [ck, n)
    QVector<int> bounds;
    bounds << 0;

    for (int c : cuts)
    {
        if (c > bounds.last() && c < units.size())
        {
            bounds << c;
        }
    }

    bounds << units.size();

    // Остальные поезда плюс уже принятые части
    Scenario work = scenario;

    if (index >= 0)
    {
        work.remove(index);
    }

    double offset = 0.0;

    for (int k = 0; k + 1 < bounds.size(); ++k)
    {
        PlacedTrain piece = edited;
        piece.timetable = Timetable();

        piece.groups = compressUnits(units.mid(bounds[k], bounds[k + 1] - bounds[k]));
        updateSummary(piece, catalog);

        if (k == 0)
        {
            piece.autoMode = edited.autoMode && piece.autopilotCapable;
        }
        else
        {
            piece.autoMode = false;

            // Голова части - там, где кончается предыдущая плюс зазор

            const Footprint walk = walkTrack(route, edited.trajectory, edited.coord,
                                             edited.dir >= 0 ? -1 : 1, offset);

            if (walk.deadEnd)
            {
                plan.errors << QObject::tr("Часть %1 не умещается на путях: не хватает %2 м.")
                                   .arg(k + 1).arg(walk.overhang, 0, 'f', 1);
                break;
            }

            piece.trajectory = walk.tailTrajectory;
            piece.coord = walk.tailCoord;
            piece.dir = -walk.tailMove;

            QString base = edited.name + QStringLiteral(" (%1)").arg(k + 1);
            int n = k + 1;

            while (work.indexOfName(base) >= 0)
            {
                base = edited.name + QStringLiteral(" (%1)").arg(++n);
            }

            piece.name = base;
        }

        // Для следующей части: длина этой плюс зазор от начала головы исходного
        double piece_len = 0.0;

        {
            const TrainSummary s = catalog.summarize(piece.groups);
            piece_len = s.length;
        }

        offset += piece_len + SPLIT_GAP;

        const PlacementCheck chk = work.check(route, piece);
        const QString who = (bounds.size() > 2) ? QObject::tr("Часть «%1»: ").arg(piece.name)
                                                : QString();

        if (chk.footprint.deadEnd)
        {
            plan.errors << who + QObject::tr("не умещается на путях, не хватает %1 м.")
                                    .arg(chk.footprint.overhang, 0, 'f', 1);
        }

        if (chk.overlap > 0.0)
        {
            plan.errors << who + QObject::tr("пересекается с поездом %1.")
                                    .arg(chk.overlapWith.join(QStringLiteral(", ")));
        }

        if (!chk.nearTrains.isEmpty())
        {
            plan.warnings << who + QObject::tr("ближе %1 м до поезда %2, в игре сцепятся.")
                                       .arg(Scenario::COUPLE_DISTANCE, 0, 'f', 0)
                                       .arg(chk.nearTrains.join(QStringLiteral(", ")));
        }

        if (chk.footprint.ambiguous)
        {
            plan.warnings << who + QObject::tr("стоит через стрелку с неопределённым положением.");
        }

        plan.pieces.push_back(piece);
        work.add(piece);
    }

    return plan;
}

} // namespace rse
