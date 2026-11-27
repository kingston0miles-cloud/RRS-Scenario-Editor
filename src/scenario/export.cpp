#include    "scenario/export.h"
#include    "scenario/stationroute.h"
#include    "scenario/autotriggers.h"

#include    "scenario/checker.h"
#include    "scenario/consist.h"
#include    "scenario/formation.h"
#include    "scenario/pathfinder.h"
#include    "scenario/trip.h"

#include    <QCryptographicHash>
#include    <cerrno>
#include    <QDateTime>
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

/// Имя резервной копии main.lua: одна копия на экспорт (метка времени в имени)
QString mainBackupName()
{
    return QStringLiteral("main.lua.bak-%1")
        .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")));
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

    // Путь строится по выбранным путям станций, отрезок за отрезком
    QVector<PathStep> steps;
    QString traj = train.trajectory;
    double coord = train.coord;
    int dir = train.dir;
    bool ok = true;

    for (const TimetableStation &st : tt.stations)
    {
        const PathResult leg = findPath(route, traj, coord, dir, st.targetTrajectory, st.targetCoord);

        if (!leg.found || leg.segments.isEmpty())
        {
            ok = false;
            break;
        }

        for (const PathStep &p : stepsOf(route, leg))
        {
            if (steps.isEmpty() || steps.last().trajectory != p.trajectory)
            {
                steps.push_back(p);
            }
        }

        traj = st.targetTrajectory;
        coord = st.targetCoord;
        dir = leg.segments.last().dir;
    }

    const QVector<StationZone> zones = steps.isEmpty() ? QVector<StationZone>() : detectZones(route, steps);

    // Зона каждой станции графика: станции между ними, которых в графике нет, не получат
    // своих маршрутов (автопилот строит их только по полям графика), поэтому уход
    // станции продлевается до подхода следующей - длинный маршрут откроет и их светофоры
    QVector<int> zone_of(tt.stations.size(), -1);
    QVector<int> target_at(tt.stations.size(), -1);

    {
        int from = 0;

        for (int k = 0; k < tt.stations.size(); ++k)
        {
            for (int j = from; j < steps.size(); ++j)
            {
                if (steps[j].trajectory == tt.stations[k].targetTrajectory)
                {
                    target_at[k] = j;
                    from = j;
                    break;
                }
            }

            // станция - последняя зона, чей входной светофор остался позади пути станции
            for (int zi = 0; zi < zones.size() && target_at[k] >= 0; ++zi)
            {
                if (zones[zi].enterStep <= target_at[k] && target_at[k] - zones[zi].enterStep <= 40)
                {
                    zone_of[k] = zi;
                }
            }
        }
    }

    // Первый выходной светофор от пути станции: уход - следующая траектория за ним
    auto removal_after = [&](int t) -> QString
    {
        for (int j = t; j < steps.size() && j <= t + 40; ++j)
        {
            if (signalAtEnd(route, steps[j].trajectory, steps[j].dir).role == SignalRole::Exit)
            {
                return (j + 1 < steps.size()) ? steps[j + 1].trajectory : QString();
            }
        }

        return QString();
    };

    for (int k = 0; k < tt.stations.size(); ++k)
    {
        const TimetableStation &st = tt.stations[k];
        QString app;
        QString rem;

        if (target_at[k] >= 0)
        {
            rem = removal_after(target_at[k]);
        }

        if (zone_of[k] >= 0 && zones[zone_of[k]].approachStep >= 0)
        {
            app = steps[zones[zone_of[k]].approachStep].trajectory;
        }

        // Между этой станцией и следующей есть станции вне графика: уход - подход следующей
        if (k + 1 < tt.stations.size() && zone_of[k] >= 0 && zone_of[k + 1] > zone_of[k] + 1)
        {
            bool unlisted_signals = false;

            for (int zi = zone_of[k] + 1; zi < zone_of[k + 1]; ++zi)
            {
                unlisted_signals = unlisted_signals || zones[zi].signalled();
            }

            const int next_app = zones[zone_of[k + 1]].approachStep;

            if (unlisted_signals && next_app >= 0)
            {
                rem = steps[next_app].trajectory;
            }
        }

        // Между уходом и подходом следующей станции на перегоне стрелки: их переводит только
        // маршрут, поэтому уход продлевается до подхода следующей станции
        if (k + 1 < tt.stations.size() && !rem.isEmpty() && zone_of[k + 1] >= 0
            && zones[zone_of[k + 1]].approachStep >= 0)
        {
            const int next_app = zones[zone_of[k + 1]].approachStep;
            int r_idx = -1;

            for (int j = std::max(0, target_at[k]); j < steps.size() && j <= next_app; ++j)
            {
                if (steps[j].trajectory == rem)
                {
                    r_idx = j;
                    break;
                }
            }

            if (r_idx >= 0 && r_idx < next_app && haulHasSwitches(route, steps, r_idx, next_app))
            {
                rem = steps[next_app].trajectory;
            }
        }

        // Траектории без станционных имён (учебные маршруты): соседи пути станции
        if (app.isEmpty() && rem.isEmpty())
        {
            for (int j = 0; j < steps.size(); ++j)
            {
                if (steps[j].trajectory == st.targetTrajectory)
                {
                    app = (j > 0) ? steps[j - 1].trajectory : QString();
                    rem = (j + 1 < steps.size()) ? steps[j + 1].trajectory : QString();
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

    // Защита ручных правок main.lua: проект помнит хеш того, что записал сам,
    // и предупреждает, если файл с тех пор менялся (этап 19, аудит 4.4)
    if (options.overwrite && isValidScenarioName(info.scenarioName))
    {
        const QString main_path = QDir(plan.scenarioDir).filePath(QStringLiteral("main.lua"));
        QFile mf(main_path);

        if (mf.open(QIODevice::ReadOnly))
        {
            const QByteArray data = mf.readAll();
            plan.mainBackupName = mainBackupName();
            plan.existingMainSha256 =
                QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());

            if (!info.exportedMainSha256.isEmpty() && info.exportedMainSha256 != plan.existingMainSha256)
            {
                plan.mainEdited = true;
                plan.warnings << QObject::tr("В main.lua есть правки после прошлого экспорта - при "
                                             "записи они будут потеряны, предыдущая версия "
                                             "сохранится в %1.").arg(plan.mainBackupName);
            }
            else if (info.exportedMainSha256.isEmpty())
            {
                plan.warnings << QObject::tr("Файл main.lua уже есть, а состояние последнего "
                                             "экспорта проекту неизвестно: при записи он будет "
                                             "заменён, предыдущая версия сохранится в %1.")
                                    .arg(plan.mainBackupName);
            }
        }
    }

    QStringList used_names;
    QStringList used_configs;

    // Продолжения: проверки и событие переименования
    for (int i = 0; i < scenario.size(); ++i)
    {
        const PlacedTrain &c = scenario.trains()[i];

        if (!c.isContinuation())
        {
            continue;
        }

        const QString who = QObject::tr("Сцепка «%1»").arg(c.name);

        if (c.name.trimmed().isEmpty() || hasBadChars(c.name)
            || QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|]")).match(c.name).hasMatch())
        {
            plan.errors << QObject::tr("%1: недопустимое имя.").arg(who);
        }

        for (int k = 0; k < scenario.size(); ++k)
        {
            if (k != i && scenario.trains()[k].name == c.name)
            {
                plan.errors << QObject::tr("%1: имя повторяется - по нему называется файл расписания.").arg(who);
                break;
            }
        }

        const FormationPlan fp = planFormation(route, scenario, c);

        if (!fp.ok)
        {
            plan.errors << QStringLiteral("%1: %2").arg(who, fp.error);
            continue;
        }

        for (const QString &w : fp.warnings)
        {
            plan.errors << QStringLiteral("%1: %2").arg(who, w);
        }

        if (!c.timetable.valid)
        {
            plan.warnings << QObject::tr("%1: нет графика - после сцепки поезд будет стоять.").arg(who);
        }
        else if (c.timetable.startTime < fp.readyTime)
        {
            plan.errors << QObject::tr("%1: отправление в %2 раньше, чем сцепка завершится (%3).")
                               .arg(who, clockText(c.timetable.startTime).left(5), clockText(fp.readyTime).left(5));
        }

        ScenarioEvent ev;
        ev.title = QObject::tr("Сцепка: «%1» становится «%2»").arg(c.formedFrom, c.name);
        ev.trigger = TriggerType::AtTime;
        ev.time = fp.readyTime;

        EventAction rename;
        rename.type = ActionType::RenameTrain;
        rename.a = c.formedFrom;
        rename.b = c.name;
        ev.actions.push_back(rename);

        EventAction rev;
        rev.type = ActionType::ReverseTrain;
        rev.a = c.name;
        ev.actions.push_back(rev);

        plan.formationEvents.push_back(ev);
    }

    for (const int i : exportOrder(scenario))
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
            if (t.noAutoEvents && !et.autoMode)
            {
                plan.warnings << QObject::tr("%1: автоматические события маршрутов отключены для "
                                             "поезда без автоведения; светофоры и стрелки по графику "
                                             "не будут переключаться.").arg(who);
            }

            if (!et.autoMode)
            {
                plan.warnings << QObject::tr("%1: движение по графику не будет автоматическим без "
                                             "автопилота; маршруты и сигналы по графику редактор "
                                             "сформирует отдельными событиями.").arg(who);
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

    // Расписания друг с другом: пересечения и заторы (формирование проверено выше)
    {
        CheckOptions copt;
        copt.startTime = plan.startTime;

        for (const ScheduleIssue &is : checkSchedules(route, scenario, copt))
        {
            if (is.severity == IssueSeverity::Info || is.kind == QLatin1String("formation")
                || is.kind == QLatin1String("no-bot") || is.kind == QLatin1String("before-start"))
            {
                continue;
            }

            plan.warnings << QObject::tr("Проверка графиков: %1").arg(is.text);
        }
    }

    {
        // Поезда без автоведения не получают график внутри RRS: симулятор
        // передаёт timetable только компонентам Autopilot. Поэтому события,
        // открывающие маршруты и сигналы по графику, для таких поездов обязательны.
        // Флажок экспорта управляет только дополнительной обработкой поездов-ботов
        // и координацией их маршрутов с остальными составами.
        AutoTriggerOptions ato;
        ato.startTime = plan.startTime;
        ato.includeAutoModeTrains = options.autoTriggers;

        const AutoTriggerPlan at = planAutoTriggers(route, scenario, ato);
        plan.autoEvents = at.events;

        for (const QString &w : at.warnings)
        {
            plan.warnings << QObject::tr("Маршруты: %1").arg(w);
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
// Разложить предупреждения по важности: что остановит поезд, где возможны
// заторы, остальное (этап 19, аудит 4.3)
//------------------------------------------------------------------------------
ExportWarningGroups groupExportWarnings(const QStringList &warnings)
{
    static const QStringList stop_keys = {
        QStringLiteral("не откроется"), QStringLiteral("не строится"),
        QStringLiteral("проехать нельзя"), QStringLiteral("нет графика"),
        QStringLiteral("будет стоять"), QStringLiteral("бот выключен"),
        QStringLiteral("автоведение не заработает"),
        QStringLiteral("маршрута нет"), QStringLiteral("не содержит станционных светофоров"),
        QStringLiteral("будут потеряны") };
    static const QStringList jam_keys = {
        QStringLiteral("Проверка графиков"), QStringLiteral("одновременно"),
        QStringLiteral("будет ждать"), QStringLiteral("затор"), QStringLiteral("Маршруты:") };

    ExportWarningGroups g;

    for (const QString &w : warnings)
    {
        bool is_stop = false;
        bool is_jam = false;

        for (const QString &k : stop_keys) is_stop = is_stop || w.contains(k);
        for (const QString &k : jam_keys) is_jam = is_jam || w.contains(k);

        (is_stop ? g.stop : (is_jam ? g.jam : g.info)) << w;
    }

    return g;
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

    const QVector<ScenarioEvent> own = info.events + plan.formationEvents;

    s += luaEvents(own);

    // Триггеры по графикам редактор создаёт заново при каждом экспорте, а при импорте
    // отбрасывает: они заключены в маркеры
    if (!plan.autoEvents.isEmpty())
    {
        s += QStringLiteral("\n-- [rse-auto begin] триггеры по графикам, создаются редактором\n");
        s += luaEvents(plan.autoEvents, own.size());
        s += QStringLiteral("-- [rse-auto end]\n");
    }

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

    const QVector<ScenarioEvent> all_events = info.events + plan.formationEvents + plan.autoEvents;

    if (!all_events.isEmpty())
    {
        s += QStringLiteral("### События\n\n");

        for (const ScenarioEvent &e : all_events)
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

    for (const PlacedTrain &c : scenario.trains())
    {
        if (!c.isContinuation())
        {
            continue;
        }

        s += QStringLiteral("- **%1** — поезд после сцепки: после прибытия «%2» к вагонам «%3» и сцепки\n")
             .arg(c.name, c.formedFrom, c.formedWagons);

        for (const TimetableStation &st : c.timetable.stations)
        {
            s += QStringLiteral("  - %1: %2").arg(st.name, clockText(st.arrival, false));

            if (st.stops())
            {
                s += QStringLiteral(" – %1").arg(clockText(st.departure, false));
            }

            s += QLatin1Char('\n');
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

        // Перезапись состава разрешена вместе с перезаписью сценария:
        // при повторном экспорте файл состава уже существует в cfg/trains
        if (!catalog.saveTrain(et.config, options.overwrite, &err))
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

    // Резервная копия предыдущей версии main.lua (этап 19, аудит 4.4): если файл есть
    // и его содержимое отличается от нового, прежняя версия сохраняется рядом
    {
        const QString main_path = dir.filePath(QStringLiteral("main.lua"));
        const QByteArray main_data = luaMain(scenario, plan, options.info).toUtf8();

        QFile old(main_path);
        QByteArray old_data;

        if (old.open(QIODevice::ReadOnly))
        {
            old_data = old.readAll();

            if (old_data != main_data)
            {
                const QString name = plan.mainBackupName.isEmpty() ? mainBackupName()
                                                                   : plan.mainBackupName;

                // Несколько экспортов в одну секунду: метка времени в имени
                // совпадает - к копии добавляется номер, каждая прежняя версия
                // main.lua сохраняется под своим именем
                QString dst = dir.filePath(name);

                for (int n = 2; QFile::exists(dst) && n < 100; ++n)
                {
                    dst = dir.filePath(QStringLiteral("%1-%2").arg(name).arg(n));
                }

                errno = 0;

                if (!QFile::copy(main_path, dst))
                {
                    return fail(QObject::tr("Не удалось сохранить резервную копию %1: errno %2")
                                    .arg(QDir(plan.scenarioDir).relativeFilePath(dst)).arg(errno));
                }
            }
        }

        if (!write(main_path, main_data)
            || !write(dir.filePath(QStringLiteral("README.md")),
                      readmeText(scenario, plan, options.info, route).toUtf8()))
        {
            return false;
        }
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

        // Подход и уход пишутся в расписание всегда, как в сценариях разработчиков
        // игры: движок строит маршруты по графику для любого поезда - и с
        // автоведением, и без него, а события по триггерам остаются страховкой

        if (!write(dir.filePath(QStringLiteral("timetable/%1.xml").arg(t.name)),
                   timetableXml(t, approach, removal)))
        {
            return false;
        }
    }

    // Пакет-дополнение: копия записанного сценария в отдельное дерево (этап 29)
    if (!options.addonDir.trimmed().isEmpty()
        && !buildAddonPackage(route, scenario, catalog, plan, dir.absolutePath(),
                              options.addonDir, error))
    {
        return false;
    }

    return true;
}

//------------------------------------------------------------------------------
// Копирование каталога со всем содержимым
//------------------------------------------------------------------------------
static bool copyDirTree(const QString &src, const QString &dst)
{
    QDir s(src);

    if (!QDir().mkpath(dst))
    {
        return false;
    }

    for (const QFileInfo &entry : s.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot))
    {
        if (entry.isDir())
        {
            if (!copyDirTree(entry.absoluteFilePath(), QDir(dst).filePath(entry.fileName())))
            {
                return false;
            }
        }
        else if (!QFile::copy(entry.absoluteFilePath(), QDir(dst).filePath(entry.fileName())))
        {
            return false;
        }
    }

    return true;
}

//------------------------------------------------------------------------------
// Пакет-дополнение (этап 29): дерево, копируемое поверх установленной игры
//------------------------------------------------------------------------------
bool buildAddonPackage(const Route &route, const Scenario &scenario,
                       const VehicleCatalog &catalog, const ExportPlan &plan,
                       const QString &scenario_dir, const QString &addon_dir,
                       QString *error)
{
    if (addon_dir.trimmed().isEmpty())
    {
        if (error != nullptr)
        {
            *error = QObject::tr("Каталог пакета-дополнения не задан.");
        }

        return false;
    }

    const QString route_name = QFileInfo(route.dir()).fileName();
    const QString scenario_name = QFileInfo(scenario_dir).fileName();
    const QDir addon(addon_dir);

    if (!addon.mkpath(QStringLiteral(".")))
    {
        if (error != nullptr)
        {
            *error = QObject::tr("Не удалось создать каталог пакета: %1").arg(addon_dir);
        }

        return false;
    }

    // Сценарий: прежний пакет того же сценария заменяется целиком
    const QString dst_scenario = addon.filePath(
        QStringLiteral("routes/%1/scenarios/%2").arg(route_name, scenario_name));

    if (QFileInfo::exists(dst_scenario) && !QDir(dst_scenario).removeRecursively())
    {
        if (error != nullptr)
        {
            *error = QObject::tr("Не удалось заменить прежний пакет сценария: %1").arg(dst_scenario);
        }

        return false;
    }

    if (!copyDirTree(scenario_dir, dst_scenario))
    {
        if (error != nullptr)
        {
            *error = QObject::tr("Не удалось скопировать сценарий в пакет: %1").arg(dst_scenario);
        }

        return false;
    }

    // Составы из плана экспорта (configName) из cfg/trains игры
    const QString dst_trains = addon.filePath(QStringLiteral("cfg/trains"));

    QStringList names;

    for (const ExportTrain &et : plan.trains)
    {
        if (!names.contains(et.configName))
        {
            names << et.configName;
        }
    }

    for (const QString &name : std::as_const(names))
    {
        const QString src = catalog.trainFilePath(name);

        if (!QFileInfo::exists(src))
        {
            continue;   // состав пришёл из игры, а не записан редактором - он и так есть
        }

        addon.mkpath(dst_trains);

        const QString dst = QDir(dst_trains).filePath(name + QStringLiteral(".xml"));

        // Повторная сборка пакета: заменяем прежний файл
        if (QFileInfo::exists(dst) && !QFile::remove(dst))
        {
            if (error != nullptr)
            {
                *error = QObject::tr("Не удалось заменить прежний состав в пакете: %1").arg(dst);
            }

            return false;
        }

        if (!QFile::copy(src, dst))
        {
            if (error != nullptr)
            {
                *error = QObject::tr("Не удалось скопировать состав «%1» в пакет").arg(name);
            }

            return false;
        }
    }

    // Инструкция пакета
    QFile readme(addon.filePath(QStringLiteral("README-package.md")));

    if (readme.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        const QString text = QStringLiteral(
            "# Пакет-дополнение «%1»\n\n"
            "Собран редактором сценариев RRS. Содержимое этого каталога копируется поверх "
            "установленной игры:\n\n"
            "- `routes/%2/scenarios/%3/` - сценарий (main.lua, README.md, расписания);\n"
            "- `cfg/trains/` - составы, созданные редактором для сценария.\n\n"
            "После копирования сценарий появится в списке сценариев маршрута.\n")
            .arg(scenario_name, route_name, scenario_name);

        readme.write(text.toUtf8());
    }

    return true;
}

} // namespace rse
