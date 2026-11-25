#include    "scenario/events.h"

#include    "scenario/pathfinder.h"

#include    <QObject>

namespace rse
{

namespace
{

const ActionSpec SPECS[] =
{
    { "Поездной маршрут", "buildTrainRoute", 2, true, "От траектории", "До траектории" },
    { "Маневровый маршрут", "buildShuntingRoute", 2, true, "От траектории", "До траектории" },
    { "Стрелки по маршруту", "setSwitchsAlongRoute", 2, true, "От траектории", "До траектории" },
    { "Стрелка в плюс", "switchFwd", 1, false, "Стрелка", "" },
    { "Стрелка в минус", "switchBwd", 1, false, "Стрелка", "" },
    { "Открыть поездной сигнал", "openSignal", 1, true, "Стык (стрелка)", "" },
    { "Открыть маневровый сигнал", "openShuntingSignal", 1, true, "Стык (стрелка)", "" },
    { "Открыть пригласительный сигнал", "openCallSignal", 1, true, "Стык (стрелка)", "" },
    { "Закрыть сигнал", "closeSignal", 1, true, "Стык (стрелка)", "" },
    { "Переименовать поезд", "renameTrain", 2, false, "Поезд", "Новое имя" },
    { "Развернуть поезд", "reverseTrain", 1, false, "Поезд", "" }
};

QString dirText(int dir)
{
    return dir >= 0 ? QStringLiteral("1") : QStringLiteral("-1");
}

bool routeReachable(const Route &route, const QString &from, const QString &to, int dir)
{
    const TrajectoryData *a = route.find(from);
    const TrajectoryData *b = route.find(to);

    if (a == nullptr || b == nullptr)
    {
        return true;   // про это скажет другая проверка
    }

    const double c0 = (dir > 0) ? 0.0 : a->length;

    for (double c1 : { 0.0, b->length })
    {
        if (findPath(route, from, c0, dir, to, c1).found)
        {
            return true;
        }
    }

    return false;
}

} // namespace

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
const ActionSpec &actionSpec(ActionType type)
{
    return SPECS[static_cast<int>(type)];
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
const QVector<ActionType> &allActionTypes()
{
    static const QVector<ActionType> all = []
    {
        QVector<ActionType> v;

        for (int i = 0; i < static_cast<int>(sizeof(SPECS) / sizeof(SPECS[0])); ++i)
        {
            v.push_back(static_cast<ActionType>(i));
        }

        return v;
    }();

    return all;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
QString triggerTitle(TriggerType type)
{
    switch (type)
    {
    case TriggerType::AtTime: return QObject::tr("В заданное время");
    case TriggerType::AfterStart: return QObject::tr("Через время после начала игры");
    case TriggerType::TrajBusy: return QObject::tr("Траектория занята поездом");
    case TriggerType::TrajFree: return QObject::tr("Траектория освобождена поездом");
    case TriggerType::AutoApproach: return QObject::tr("Автоматический пропуск поездов");
    }

    return QString();
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
QString triggerTime(int sec, bool rel)
{
    sec = std::max(0, sec);

    if (!rel)
    {
        sec %= 86400;
    }

    return QStringLiteral("%1%2:%3:%4").arg(rel ? "+" : "")
            .arg(sec / 3600, 2, 10, QLatin1Char('0'))
            .arg((sec / 60) % 60, 2, 10, QLatin1Char('0'))
            .arg(sec % 60, 2, 10, QLatin1Char('0'));
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
QString luaQuote(const QString &s)
{
    QString r = s;

    r.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    r.replace(QLatin1Char('"'), QStringLiteral("\\\""));
    r.replace(QLatin1Char('\n'), QStringLiteral("\\n"));
    r.replace(QLatin1Char('\r'), QString());

    return QStringLiteral("\"") + r + QStringLiteral("\"");
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
QString actionText(const EventAction &a)
{
    const ActionSpec &sp = actionSpec(a.type);
    QString s = QObject::tr(sp.title) + QStringLiteral(": ") + a.a;

    if (sp.strings > 1)
    {
        s += (a.type == ActionType::RenameTrain ? QStringLiteral(" → ") : QStringLiteral(" → ")) + a.b;
    }

    if (sp.dir && a.dir != 0)
    {
        s += QObject::tr(" (напр. %1)").arg(dirText(a.dir));
    }

    return s;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
QString triggerText(const ScenarioEvent &e)
{
    switch (e.trigger)
    {
    case TriggerType::AtTime:
        return QObject::tr("в %1").arg(triggerTime(e.time, false));

    case TriggerType::AfterStart:
        return QObject::tr("через %1 после начала").arg(triggerTime(e.time, true).mid(1));

    case TriggerType::TrajBusy:
    case TriggerType::TrajFree:
    {
        QString s = (e.trigger == TriggerType::TrajBusy) ? QObject::tr("занятие %1") : QObject::tr("освобождение %1");
        s = s.arg(e.traj);

        if (!e.train.isEmpty())
        {
            s += QObject::tr(" поездом %1").arg(e.train);
        }

        if (e.delaySec > 0)
        {
            s += QObject::tr(", через %1").arg(triggerTime(e.delaySec, true).mid(1));
        }

        return s;
    }

    case TriggerType::AutoApproach:
        return QObject::tr("авто-пропуск %1 → %2 (напр. %3)").arg(e.traj, e.traj2, dirText(e.dir));
    }

    return QString();
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
EventCheck checkEvent(const Route &route, const Scenario &scenario, const ScenarioEvent &e,
                      int start_time)
{
    EventCheck c;

    auto need_traj = [&](const QString &name, const QString &what)
    {
        if (name.trimmed().isEmpty())
        {
            c.errors << QObject::tr("Не задано: %1.").arg(what);
        }
        else if (route.isLoaded() && route.find(name) == nullptr)
        {
            c.warnings << QObject::tr("Траектории «%1» нет в маршруте.").arg(name);
        }
    };

    auto need_train = [&](const QString &name, const QString &what)
    {
        if (name.trimmed().isEmpty())
        {
            c.errors << QObject::tr("Не задано: %1.").arg(what);
        }
        else if (scenario.indexOfName(name) < 0)
        {
            c.warnings << QObject::tr("Поезда «%1» нет в сценарии (возможно, он переименован другим событием).").arg(name);
        }
    };

    switch (e.trigger)
    {
    case TriggerType::AtTime:
        if (e.time < start_time)
        {
            c.warnings << QObject::tr("Время %1 раньше начала игры — триггер на прошедшее время не сработает.")
                          .arg(triggerTime(e.time, false));
        }
        break;

    case TriggerType::AfterStart:
        break;

    case TriggerType::TrajBusy:
    case TriggerType::TrajFree:
        need_traj(e.traj, QObject::tr("траектория триггера"));

        if (!e.train.isEmpty())
        {
            need_train(e.train, QObject::tr("поезд триггера"));
        }
        break;

    case TriggerType::AutoApproach:
        need_traj(e.traj, QObject::tr("начальная траектория"));
        need_traj(e.traj2, QObject::tr("конечная траектория"));

        if (!e.traj.isEmpty() && !e.traj2.isEmpty() && routeReachable(route, e.traj, e.traj2, e.dir) == false)
        {
            c.warnings << QObject::tr("От «%1» до «%2» в направлении %3 пути нет.")
                          .arg(e.traj, e.traj2, dirText(e.dir));
        }

        return c;
    }

    if (e.actions.isEmpty())
    {
        c.errors << QObject::tr("У события нет действий.");
    }

    for (const EventAction &a : e.actions)
    {
        const ActionSpec &sp = actionSpec(a.type);
        const QString title = QObject::tr(sp.title);

        switch (a.type)
        {
        case ActionType::BuildTrainRoute:
        case ActionType::BuildShuntingRoute:
        case ActionType::SetSwitchesAlongRoute:
            need_traj(a.a, title + QObject::tr(": начальная траектория"));
            need_traj(a.b, title + QObject::tr(": конечная траектория"));

            if (!a.a.isEmpty() && !a.b.isEmpty() && !routeReachable(route, a.a, a.b, a.dir))
            {
                c.warnings << QObject::tr("%1: от «%2» до «%3» в направлении %4 пути нет.")
                              .arg(title, a.a, a.b, dirText(a.dir));
            }
            break;

        case ActionType::SwitchFwd:
        case ActionType::SwitchBwd:
        case ActionType::OpenSignal:
        case ActionType::OpenShuntingSignal:
        case ActionType::OpenCallSignal:
        case ActionType::CloseSignal:
        {
            if (a.a.trimmed().isEmpty())
            {
                c.errors << QObject::tr("%1: не задан стык (стрелка).").arg(title);
                break;
            }

            bool found = false;

            for (const SwitchData &sw : route.switches())
            {
                if (sw.name == a.a)
                {
                    found = true;
                    break;
                }
            }

            if (route.isLoaded() && !found)
            {
                c.warnings << QObject::tr("%1: стрелки «%2» нет в топологии.").arg(title, a.a);
            }
            break;
        }

        case ActionType::RenameTrain:
            need_train(a.a, title + QObject::tr(": поезд"));

            if (a.b.trimmed().isEmpty())
            {
                c.errors << QObject::tr("%1: не задано новое имя.").arg(title);
            }
            break;

        case ActionType::ReverseTrain:
            need_train(a.a, title + QObject::tr(": поезд"));
            break;
        }
    }

    return c;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
QString luaEvents(const QVector<ScenarioEvent> &events, int first_index)
{
    QString s;

    for (int i = 0; i < events.size(); ++i)
    {
        const ScenarioEvent &e = events[i];
        const QString id = QStringLiteral("event%1").arg(first_index + i + 1);

        s += QStringLiteral("\n-- %1\n").arg(e.title.isEmpty() ? QObject::tr("Событие %1").arg(i + 1)
                                                               : e.title.simplified());
        s += QStringLiteral("-- Триггер: %1\n").arg(triggerText(e).simplified());

        if (e.trigger == TriggerType::AutoApproach)
        {
            s += QStringLiteral("setAutoApproachTrigger(%1, %2, %3)\n")
                 .arg(luaQuote(e.traj), luaQuote(e.traj2), dirText(e.dir));
            continue;
        }

        s += QStringLiteral("function %1_actions()\n").arg(id);

        for (const EventAction &a : e.actions)
        {
            const ActionSpec &sp = actionSpec(a.type);
            QStringList args;

            args << luaQuote(a.a);

            if (sp.strings > 1)
            {
                args << luaQuote(a.b);
            }

            if (sp.dir && a.dir != 0)
            {
                args << dirText(a.dir);
            }

            s += QStringLiteral("\t%1(%2)\n").arg(QLatin1String(sp.luaName), args.join(QStringLiteral(", ")));
        }

        s += QStringLiteral("end\n");

        QString handler = id + QStringLiteral("_actions");

        if (e.delaySec > 0 && (e.trigger == TriggerType::TrajBusy || e.trigger == TriggerType::TrajFree))
        {
            s += QStringLiteral("\nfunction %1_fire()\n\tsetPostEventTimeTrigger(%2, %3)\nend\n")
                 .arg(id, luaQuote(triggerTime(e.delaySec, true)), handler);
            handler = id + QStringLiteral("_fire");
        }

        switch (e.trigger)
        {
        case TriggerType::AtTime:
            s += QStringLiteral("setTimeTrigger(%1, %2)\n").arg(luaQuote(triggerTime(e.time, false)), handler);
            break;

        case TriggerType::AfterStart:
            s += QStringLiteral("setTimeTrigger(%1, %2)\n").arg(luaQuote(triggerTime(e.time, true)), handler);
            break;

        case TriggerType::TrajBusy:
        case TriggerType::TrajFree:
        {
            const bool busy = (e.trigger == TriggerType::TrajBusy);

            if (e.train.isEmpty())
            {
                s += QStringLiteral("%1(%2, %3)\n")
                     .arg(busy ? "setOnTrajBusyTrigger" : "setOnTrajFreeTrigger", luaQuote(e.traj), handler);
            }
            else
            {
                s += QStringLiteral("%1(%2, %3, %4)\n")
                     .arg(busy ? "setOnTrajBusyByTrainTrigger" : "setOnTrajFreeByTrainTrigger",
                          luaQuote(e.traj), luaQuote(e.train), handler);
            }
            break;
        }

        case TriggerType::AutoApproach:
            break;
        }
    }

    return s;
}

} // namespace rse
