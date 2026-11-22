#include    "scenario/export.h"

#include    "scenario/consist.h"
#include    "scenario/pathfinder.h"
#include    "scenario/trip.h"

#include    <QDir>
#include    <QFile>
#include    <QFileInfo>
#include    <QRegularExpression>
#include    <QSaveFile>
#include    <QXmlStreamWriter>

#include    <algorithm>

namespace rse
{

namespace
{

QString luaString(const QString &s)
{
    QString r = s;

    r.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    r.replace(QLatin1Char('"'), QStringLiteral("\\\""));

    return QStringLiteral("\"") + r + QStringLiteral("\"");
}

QString clockText(int sec, bool with_seconds)
{
    sec = ((sec % 86400) + 86400) % 86400;

    QString s = QStringLiteral("%1:%2").arg(sec / 3600, 2, 10, QLatin1Char('0'))
                                      .arg((sec / 60) % 60, 2, 10, QLatin1Char('0'));

    if (with_seconds && (sec % 60 != 0))
    {
        s += QStringLiteral(":%1").arg(sec % 60, 2, 10, QLatin1Char('0'));
    }

    return s;
}

bool sameGroups(const QVector<ConsistGroup> &a, const QVector<ConsistGroup> &b)
{
    const QVector<ConsistGroup> ca = compressUnits(flattenGroups(a));
    const QVector<ConsistGroup> cb = compressUnits(flattenGroups(b));

    if (ca.size() != cb.size())
    {
        return false;
    }

    for (int i = 0; i < ca.size(); ++i)
    {
        const ConsistGroup &x = ca[i];
        const ConsistGroup &y = cb[i];

        if (x.configId() != y.configId() || x.count != y.count || x.forward != y.forward
            || x.brakeShoes != y.brakeShoes || x.module != y.module
            || !qFuzzyCompare(x.payloadCoeff + 1.0, y.payloadCoeff + 1.0))
        {
            return false;
        }
    }

    return true;
}

/// Только символы, допустимые в имени файла cfg/trains
QString configSafe(const QString &s)
{
    QString r;

    for (const QChar c : s)
    {
        if ((c.unicode() < 128) && (c.isLetterOrNumber() || c == QLatin1Char('_') || c == QLatin1Char('-')))
        {
            r += c;
        }
    }

    return r;
}

bool hasBadChars(const QString &s)
{
    for (const QChar c : s)
    {
        if (c.unicode() < 32)
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
bool isValidScenarioName(const QString &name)
{
    static const QRegularExpression re(QStringLiteral("^[A-Za-z0-9_\\-]+$"));

    return re.match(name).hasMatch();
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void timetableApproachRemoval(const Route &route, const PlacedTrain &train,
                              QStringList *approach, QStringList *removal)
{
    approach->clear();
    removal->clear();

    const Timetable &tt = train.timetable;

    if (!tt.valid)
    {
        return;
    }

    const PathResult path = findPath(route, train.trajectory, train.coord, train.dir,
                                     tt.endTrajectory, tt.endCoord);

    int from_seg = 0;

    for (int k = 0; k < tt.stations.size(); ++k)
    {
        const TimetableStation &st = tt.stations[k];
        QString app;
        QString rem;

        if (path.found)
        {
            for (int i = from_seg; i < path.segments.size(); ++i)
            {
                const PathSegment &seg = path.segments[i];
                const double lo = std::min(seg.from, seg.to) - 0.5;
                const double hi = std::max(seg.from, seg.to) + 0.5;

                if (seg.trajectory == st.targetTrajectory && st.targetCoord >= lo && st.targetCoord <= hi)
                {
                    if (i > 0)
                    {
                        app = path.segments[i - 1].trajectory;
                    }

                    if (i + 1 < path.segments.size())
                    {
                        rem = path.segments[i + 1].trajectory;
                    }

                    from_seg = i;
                    break;
                }
            }
        }

        // У первой станции нет подхода, у последней - ухода
        if (k == 0 || app == st.targetTrajectory)
        {
            app.clear();
        }

        if (k + 1 == tt.stations.size() || rem == st.targetTrajectory)
        {
            rem.clear();
        }

        approach->push_back(app);
        removal->push_back(rem);
    }
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
QByteArray timetableXml(const PlacedTrain &train, const QStringList &approach,
                        const QStringList &removal)
{
    QByteArray data;
    QXmlStreamWriter w(&data);

    w.setAutoFormatting(true);
    w.setAutoFormattingIndent(-1);
    w.writeStartDocument();
    w.writeStartElement(QStringLiteral("Config"));

    const Timetable &tt = train.timetable;

    for (int k = 0; k < tt.stations.size(); ++k)
    {
        const TimetableStation &st = tt.stations[k];

        w.writeStartElement(QStringLiteral("Station"));
        w.writeTextElement(QStringLiteral("Name"), st.name);

        if (k > 0)
        {
            w.writeTextElement(QStringLiteral("ArrivalTime"), clockText(st.arrival, true));
        }

        if (k + 1 < tt.stations.size())
        {
            w.writeTextElement(QStringLiteral("DepartureTime"), clockText(st.departure, true));
        }

        w.writeTextElement(QStringLiteral("TargetTraj"), st.targetTrajectory);
        w.writeTextElement(QStringLiteral("TargetCoord"), QString::number(st.targetCoord, 'f', 1));

        const QString app = !st.approachTrajectory.isEmpty() ? st.approachTrajectory : approach.value(k);
        const QString rem = !st.removalTrajectory.isEmpty() ? st.removalTrajectory : removal.value(k);

        if (!app.isEmpty())
        {
            w.writeTextElement(QStringLiteral("ApproachTraj"), app);
        }

        if (!rem.isEmpty())
        {
            w.writeTextElement(QStringLiteral("RemovalTraj"), rem);
        }

        if (st.rightPlatform >= 0)
        {
            w.writeTextElement(QStringLiteral("IsRightPlatform"), st.rightPlatform ? "1" : "0");
        }

        if (st.leftPlatform >= 0)
        {
            w.writeTextElement(QStringLiteral("IsLeftPlatform"), st.leftPlatform ? "1" : "0");
        }

        if (st.visible >= 0)
        {
            w.writeTextElement(QStringLiteral("IsVisible"), st.visible ? "1" : "0");
        }

        w.writeEndElement();
    }

    w.writeEndElement();
    w.writeEndDocument();

    return data;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
ExportPlan planExport(const Route &route, const VehicleCatalog &catalog, const Scenario &scenario,
                      const ExportOptions &options)
{
    ExportPlan plan;

    const ProjectInfo &info = options.info;

    if (!isValidScenarioName(info.scenarioName))
    {
        plan.errors << QObject::tr("Имя сценария должно состоять из латинских букв, цифр, «_» и «-».");
    }

    if (!route.isLoaded())
    {
        plan.errors << QObject::tr("Маршрут не загружен.");
        return plan;
    }

    plan.scenarioDir = QDir(route.dir()).filePath(QStringLiteral("scenarios/") + info.scenarioName);
    plan.startTime = (info.startTime >= 0) ? info.startTime : suggestStartTime(scenario);

    if (!options.overwrite && QFileInfo::exists(plan.scenarioDir) && isValidScenarioName(info.scenarioName))
    {
        plan.errors << QObject::tr("Каталог сценария уже существует: %1. Включите замену или "
                                   "выберите другое имя.").arg(plan.scenarioDir);
    }

    if (scenario.isEmpty())
    {
        plan.errors << QObject::tr("В сценарии нет поездов.");
        return plan;
    }

    if (!catalog.isLoaded())
    {
        plan.errors << QObject::tr("Не загружен каталог игры: нужен для записи составов в cfg/trains.");
        return plan;
    }

    QStringList used_names;
    QStringList used_configs;

    for (int i = 0; i < scenario.size(); ++i)
    {
        const PlacedTrain &t = scenario.trains()[i];

        ExportTrain et;
        et.index = i;
        et.luaVar = t.luaVar;

        if (et.luaVar.isEmpty())
        {
            et.luaVar = QStringLiteral("train%1").arg(i + 1);
        }

        const QString who = QObject::tr("Поезд «%1»").arg(t.name);

        if (t.name.trimmed().isEmpty() || hasBadChars(t.name))
        {
            plan.errors << QObject::tr("Поезд №%1: недопустимое имя.").arg(i + 1);
        }
        else if (used_names.contains(t.name))
        {
            plan.errors << QObject::tr("%1: имя повторяется — по нему называется файл расписания.").arg(who);
        }

        used_names << t.name;

        if (t.timetable.valid && QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|]")).match(t.name).hasMatch())
        {
            plan.errors << QObject::tr("%1: в имени есть символы, недопустимые в имени файла "
                                       "расписания (\\ / : * ? \" < > |).").arg(who);
        }

        const PlacementCheck chk = scenario.check(route, t, i);

        if (chk.overlap > 0.0)
        {
            plan.errors << QObject::tr("%1: пересекается с поездом %2.").arg(who, chk.overlapWith.join(QStringLiteral(", ")));
        }

        if (chk.footprint.deadEnd)
        {
            plan.errors << QObject::tr("%1: не помещается на путях (упирается в тупик).").arg(who);
        }

        if (route.find(t.trajectory) == nullptr)
        {
            plan.errors << QObject::tr("%1: траектории «%2» нет в маршруте.").arg(who, t.trajectory);
        }

        // Конфиг состава
        const TrainConfigData *src = catalog.findTrain(t.sourceTrain);

        if (src != nullptr && sameGroups(src->groups, t.groups))
        {
            et.configName = src->name;
        }
        else
        {
            // Тот же состав уже создавался для другого поезда?
            bool reused = false;

            for (const ExportTrain &prev : std::as_const(plan.trains))
            {
                if (prev.writeConfig && sameGroups(prev.config.groups, t.groups))
                {
                    et.configName = prev.configName;
                    reused = true;
                    break;
                }
            }

            if (!reused)
            {
                QString base = info.scenarioName + QLatin1Char('-') + configSafe(t.name);

                if (configSafe(t.name).isEmpty())
                {
                    base = info.scenarioName + QStringLiteral("-train") + QString::number(i + 1);
                }

                QString name = base;
                int n = 2;

                while (catalog.findTrain(name) != nullptr || used_configs.contains(name))
                {
                    // Такой состав уже есть в каталоге (например, прошлый экспорт)?
                    const TrainConfigData *ex = catalog.findTrain(name);

                    if (ex != nullptr && sameGroups(ex->groups, t.groups))
                    {
                        break;
                    }

                    name = base + QLatin1Char('-') + QString::number(n++);
                }

                const TrainConfigData *existing = catalog.findTrain(name);

                et.configName = name;

                if (existing == nullptr)
                {
                    et.writeConfig = true;

                    TrainConfigData cfg;

                    if (src != nullptr)
                    {
                        cfg = *src;
                    }

                    cfg.name = name;
                    cfg.title = t.title.isEmpty() ? t.name : t.title;
                    cfg.description = QObject::tr("Создан редактором сценариев для сценария %1")
                                      .arg(info.scenarioName);
                    cfg.groups = t.groups;
                    et.config = cfg;

                    const QStringList problems = catalog.validateTrain(cfg);

                    for (const QString &p : problems)
                    {
                        plan.errors << QStringLiteral("%1: %2").arg(who, p);
                    }

                    used_configs << name;
                }
            }
        }

        et.autoMode = t.autoMode && t.autopilotCapable;

        if (t.autoMode && !t.autopilotCapable)
        {
            plan.warnings << QObject::tr("%1: бот включён, но у крайней единицы нет автопилота — "
                                         "автоведение не заработает.").arg(who);
        }

        if (t.timetable.valid)
        {
            if (!et.autoMode)
            {
                plan.warnings << QObject::tr("%1: график задан, но бот выключен — по графику ведёт "
                                             "поезд только автопилот.").arg(who);
            }

            if (t.timetable.startTime < plan.startTime)
            {
                plan.warnings << QObject::tr("%1: по графику отправление в %2, раньше начала игры (%3).")
                                 .arg(who, formatClock(t.timetable.startTime).left(5),
                                      formatClock(plan.startTime).left(5));
            }
        }

        plan.trains.push_back(et);
    }

    // Имена переменных Lua не должны совпадать между собой и с сохранённым кодом
    {
        QStringList taken;

        for (int i = 0; i < plan.trains.size(); ++i)
        {
            ExportTrain &et = plan.trains[i];
            const bool imported = !scenario.trains()[et.index].luaVar.isEmpty();

            if (imported)
            {
                taken << et.luaVar;
            }
        }

        for (ExportTrain &et : plan.trains)
        {
            if (!scenario.trains()[et.index].luaVar.isEmpty())
            {
                continue;
            }

            int n = et.index + 1;
            QString v = QStringLiteral("train%1").arg(n);

            while (taken.contains(v))
            {
                v = QStringLiteral("train%1").arg(++n);
            }

            et.luaVar = v;
            taken << v;
        }
    }

    for (int k = 0; k < info.events.size(); ++k)
    {
        const EventCheck ec = checkEvent(route, scenario, info.events[k], plan.startTime);
        const QString who = QObject::tr("Событие %1 «%2»").arg(k + 1).arg(info.events[k].title);

        for (const QString &e : ec.errors)
        {
            plan.errors << QStringLiteral("%1: %2").arg(who, e);
        }

        for (const QString &w : ec.warnings)
        {
            plan.warnings << QStringLiteral("%1: %2").arg(who, w);
        }
    }

    plan.files << QStringLiteral("main.lua") << QStringLiteral("README.md");

    for (const PlacedTrain &t : scenario.trains())
    {
        if (t.timetable.valid)
        {
            plan.files << QStringLiteral("timetable/%1.xml").arg(t.name);
        }
    }

    for (const ExportTrain &et : std::as_const(plan.trains))
    {
        if (et.writeConfig)
        {
            plan.files << QStringLiteral("cfg/trains/%1.xml (в каталоге игры)").arg(et.configName);
        }
    }

    return plan;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
QString luaMain(const Scenario &scenario, const ExportPlan &plan, const ProjectInfo &info)
{
    QString s;

    s += QStringLiteral("-- Сценарий «%1»: создан редактором сценариев RRS\n\n").arg(info.scenarioName);
    if (!info.opaquePre.isEmpty() || !info.opaquePost.isEmpty())
    {
        s += QStringLiteral("-- Фрагменты, не распознанные редактором при импорте, сохранены как есть\n\n");
    }

    for (const QString &block : info.opaquePre)
    {
        s += block.trimmed() + QStringLiteral("\n\n");
    }

    s += QStringLiteral("-- Время начала игры\n");
    s += QStringLiteral("setTime(%1)\n").arg(luaString(clockText(plan.startTime, true)));

    for (const ExportTrain &et : plan.trains)
    {
        const PlacedTrain &t = scenario.trains()[et.index];

        s += QStringLiteral("\n-- %1\n").arg(t.name.simplified());
        s += QStringLiteral("%1 = TrainData.new()\n").arg(et.luaVar);
        s += QStringLiteral("%1.name = %2\n").arg(et.luaVar, luaString(t.name));
        s += QStringLiteral("%1.config = %2\n").arg(et.luaVar, luaString(et.configName));
        s += QStringLiteral("%1.traj = %2\n").arg(et.luaVar, luaString(t.trajectory));
        s += QStringLiteral("%1.coord = %2\n").arg(et.luaVar).arg(t.coord, 0, 'f', 1);
        s += QStringLiteral("%1.dir = %2\n").arg(et.luaVar).arg(t.dir);
        s += QStringLiteral("%1.auto = %2\n").arg(et.luaVar, et.autoMode ? "true" : "false");
        s += QStringLiteral("\nsetTrain(%1)\n").arg(et.luaVar);
    }

    s += luaEvents(info.events);

    for (const QString &block : info.opaquePost)
    {
        s += QStringLiteral("\n") + block.trimmed() + QStringLiteral("\n");
    }

    return s;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
QString readmeText(const Scenario &scenario, const ExportPlan &plan, const ProjectInfo &info,
                   const Route &route)
{
    QString s;

    s += QStringLiteral("## %1\n\n").arg(info.scenarioName);

    if (!info.description.trimmed().isEmpty())
    {
        s += info.description.trimmed() + QStringLiteral("\n\n");
    }

    s += QStringLiteral("Маршрут: %1. Начало игры: %2.\n\n")
         .arg(route.title(), clockText(plan.startTime, false));

    if (!info.events.isEmpty())
    {
        s += QStringLiteral("### События\n\n");

        for (const ScenarioEvent &e : info.events)
        {
            s += QStringLiteral("- %1 — %2\n").arg(triggerText(e), e.title);

            for (const EventAction &a : e.actions)
            {
                s += QStringLiteral("  - %1\n").arg(actionText(a));
            }
        }

        s += QLatin1Char('\n');
    }

    s += QStringLiteral("### Поезда\n\n");

    for (const ExportTrain &et : plan.trains)
    {
        const PlacedTrain &t = scenario.trains()[et.index];

        s += QStringLiteral("- **%1** — состав `%2`, путь `%3`, %4 м%5\n")
             .arg(t.name, et.configName, t.trajectory)
             .arg(t.coord, 0, 'f', 0)
             .arg(et.autoMode ? QStringLiteral(", автоведение") : QString());

        if (t.timetable.valid)
        {
            for (const TimetableStation &st : t.timetable.stations)
            {
                s += QStringLiteral("  - %1: %2").arg(st.name, clockText(st.arrival, false));

                if (st.stops())
                {
                    s += QStringLiteral(" – %1").arg(clockText(st.departure, false));
                }

                s += QLatin1Char('\n');
            }
        }
    }

    return s;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
bool writeExport(const Route &route, const VehicleCatalog &catalog, const Scenario &scenario,
                 const ExportPlan &plan, const ExportOptions &options, QString *error)
{
    auto fail = [error](const QString &text)
    {
        if (error != nullptr)
        {
            *error = text;
        }

        return false;
    };

    if (!plan.ok())
    {
        return fail(plan.errors.join(QLatin1Char('\n')));
    }

    // Составы в cfg/trains
    for (const ExportTrain &et : plan.trains)
    {
        if (!et.writeConfig)
        {
            continue;
        }

        QString err;

        if (!catalog.saveTrain(et.config, false, &err))
        {
            return fail(err);
        }
    }

    auto write = [&fail](const QString &path, const QByteArray &data)
    {
        if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        {
            return fail(QObject::tr("Не удалось создать каталог %1").arg(QFileInfo(path).absolutePath()));
        }

        QSaveFile f(path);

        if (!f.open(QIODevice::WriteOnly))
        {
            return fail(QObject::tr("Не удалось записать %1: %2").arg(path, f.errorString()));
        }

        f.write(data);

        if (!f.commit())
        {
            return fail(QObject::tr("Не удалось записать %1: %2").arg(path, f.errorString()));
        }

        return true;
    };

    const QDir dir(plan.scenarioDir);

    if (!write(dir.filePath(QStringLiteral("main.lua")), luaMain(scenario, plan, options.info).toUtf8())
        || !write(dir.filePath(QStringLiteral("README.md")),
                  readmeText(scenario, plan, options.info, route).toUtf8()))
    {
        return false;
    }

    for (const PlacedTrain &t : scenario.trains())
    {
        if (!t.timetable.valid)
        {
            continue;
        }

        QStringList approach;
        QStringList removal;

        timetableApproachRemoval(route, t, &approach, &removal);

        if (!write(dir.filePath(QStringLiteral("timetable/%1.xml").arg(t.name)),
                   timetableXml(t, approach, removal)))
        {
            return false;
        }
    }

    return true;
}

} // namespace rse
