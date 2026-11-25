#include    "scenario/formation.h"

#include    "scenario/footprint.h"

#include    <QObject>

#include    <algorithm>

namespace rse
{

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
FormationPlan planFormation(const Route &route, const Scenario &scenario,
                            const PlacedTrain &cont)
{
    FormationPlan plan;

    plan.locoIndex = scenario.indexOfName(cont.formedFrom);
    plan.wagonsIndex = scenario.indexOfName(cont.formedWagons);

    if (plan.locoIndex < 0)
    {
        plan.error = QObject::tr("Локомотив «%1» не найден.").arg(cont.formedFrom);
        return plan;
    }

    if (plan.wagonsIndex < 0)
    {
        plan.error = QObject::tr("Вагоны «%1» не найдены.").arg(cont.formedWagons);
        return plan;
    }

    if (plan.locoIndex == plan.wagonsIndex)
    {
        plan.error = QObject::tr("Локомотив и вагоны - один и тот же поезд.");
        return plan;
    }

    const PlacedTrain &loco = scenario.trains()[plan.locoIndex];
    const PlacedTrain &wagons = scenario.trains()[plan.wagonsIndex];

    if (loco.isContinuation() || wagons.isContinuation())
    {
        plan.error = QObject::tr("Сцепку можно строить только от поезда, стоящего на пути.");
        return plan;
    }

    const Timetable &tt = loco.timetable;

    if (!tt.valid || tt.samples.isEmpty() || tt.stations.isEmpty())
    {
        plan.error = QObject::tr("У локомотива «%1» нет рассчитанного графика: поставьте "
                                 "конечную точку графика у вагонов.").arg(loco.name);
        return plan;
    }

    // Локомотив в конце графика
    plan.locoAtEnd = loco;
    plan.locoAtEnd.timetable = Timetable();
    plan.locoAtEnd.trajectory = tt.endTrajectory;
    plan.locoAtEnd.coord = tt.endCoord;
    plan.locoAtEnd.dir = tt.samples.last().dir;

    plan.arrivalTime = tt.stations.last().arrival;
    plan.readyTime = plan.arrivalTime + cont.formDelaySec;

    const Footprint lfp = computeFootprint(route, plan.locoAtEnd.trajectory, plan.locoAtEnd.coord,
                                           plan.locoAtEnd.dir, plan.locoAtEnd.length);
    const Footprint wfp = computeFootprint(route, wagons.trajectory, wagons.coord,
                                           wagons.dir, wagons.length);

    if (lfp.deadEnd)
    {
        plan.error = QObject::tr("Локомотив не умещается на путях в конечной точке графика.");
        return plan;
    }

    const double ov = overlapLength(lfp, wfp);

    if (ov > 0.0)
    {
        plan.error = QObject::tr("В конечной точке графика локомотив «%1» стоит на вагонах «%2» "
                                 "(пересечение %3 м): сдвиньте конечную точку.")
                         .arg(loco.name, wagons.name).arg(ov, 0, 'f', 1);
        return plan;
    }

    const Footprint zone = extendedFootprint(route, lfp, plan.locoAtEnd.dir, Scenario::COUPLE_DISTANCE);
    plan.couples = overlapLength(zone, wfp) > 0.0;

    if (!plan.couples)
    {
        plan.warnings << QObject::tr("В конце графика локомотив «%1» остановится дальше %2 м от "
                                     "вагонов «%3» и не сцепится с ними: поставьте конечную точку "
                                     "ближе к вагонам.")
                             .arg(loco.name).arg(Scenario::COUPLE_DISTANCE, 0, 'f', 0).arg(wagons.name);
    }

    // Новый поезд едет в обратную сторону: голова - бывший хвост локомотива
    PlacedTrain f = Scenario::reversedInPlace(route, plan.locoAtEnd);

    // Вагоны идут за локомотивом от ближнего к нему конца. Если голова вагонов смотрит
    // туда же, куда прибыл локомотив, ближний конец - их хвост
    PlacedTrain w = wagons;

    if (wagons.dir == plan.locoAtEnd.dir)
    {
        w = Scenario::reversedInPlace(route, wagons);
    }

    f.groups += w.groups;
    f.length = loco.length + wagons.length;
    f.mass = loco.mass + wagons.mass;
    f.title = QObject::tr("%1 + %2").arg(loco.title, wagons.title);
    f.autopilotCapable = loco.autopilotCapable;
    f.autoMode = loco.autoMode;
    f.sourceTrain.clear();

    f.name = cont.name;
    f.luaVar = cont.luaVar;
    f.formedFrom = cont.formedFrom;
    f.formedWagons = cont.formedWagons;
    f.formDelaySec = cont.formDelaySec;
    f.timetable = cont.timetable;

    plan.formed = f;
    plan.ok = true;

    return plan;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
QVector<int> exportOrder(const Scenario &scenario)
{
    QVector<int> order;

    for (int i = 0; i < scenario.size(); ++i)
    {
        if (!scenario.trains()[i].isVirtual())
        {
            order << i;
        }
    }

    // Вагоны переносим за локомотив (сохраняя прочий порядок)
    for (const PlacedTrain &c : scenario.trains())
    {
        if (!c.isContinuation())
        {
            continue;
        }

        const int loco = scenario.indexOfName(c.formedFrom);
        const int wag = scenario.indexOfName(c.formedWagons);

        const int pl = order.indexOf(loco);
        const int pw = order.indexOf(wag);

        if (pl >= 0 && pw >= 0 && pw < pl)
        {
            order.removeAt(pw);
            order.insert(order.indexOf(loco) + 1, wag);
        }
    }

    return order;
}

} // namespace rse
