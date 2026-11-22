#include    "scenario/project.h"

#include    <QDir>
#include    <QFile>
#include    <QFileInfo>
#include    <QJsonArray>
#include    <QJsonDocument>
#include    <QJsonObject>
#include    <QSaveFile>

#include    <algorithm>

namespace rse
{

namespace
{

constexpr int FORMAT_VERSION = 1;

QJsonObject groupToJson(const ConsistGroup &g)
{
    QJsonObject o;

    o["module_dir"] = g.module.moduleDir;
    o["module"] = g.module.module;
    o["config_dir"] = g.configDir;
    o["config"] = g.config;
    o["count"] = g.count;
    o["forward"] = g.forward;
    o["payload"] = g.payloadCoeff;
    o["brake_shoes"] = g.brakeShoes;

    return o;
}

ConsistGroup groupFromJson(const QJsonObject &o)
{
    ConsistGroup g;

    g.module.moduleDir = o["module_dir"].toString();
    g.module.module = o["module"].toString();
    g.configDir = o["config_dir"].toString();
    g.config = o["config"].toString();
    g.count = o["count"].toInt(1);
    g.forward = o["forward"].toBool(true);
    g.payloadCoeff = o["payload"].toDouble();
    g.brakeShoes = o["brake_shoes"].toBool();

    return g;
}

QJsonObject timetableToJson(const Timetable &tt)
{
    QJsonObject o;

    o["start"] = tt.startTime;
    o["end_traj"] = tt.endTrajectory;
    o["end_coord"] = tt.endCoord;
    o["total_extra_min"] = tt.totalExtraMin;

    QJsonArray stations;

    for (const TimetableStation &s : tt.stations)
    {
        QJsonObject so;

        so["name"] = s.name;
        so["arrival"] = s.arrival;
        so["departure"] = s.departure;
        so["traj"] = s.targetTrajectory;
        so["coord"] = s.targetCoord;
        so["extra_min"] = s.extraMin;
        so["approach"] = s.approachTrajectory;
        so["removal"] = s.removalTrajectory;
        so["right"] = s.rightPlatform;
        so["left"] = s.leftPlatform;
        so["visible"] = s.visible;
        stations.append(so);
    }

    o["stations"] = stations;

    QJsonArray samples;

    for (const TimetableSample &s : tt.samples)
    {
        samples.append(QJsonArray { s.time, s.trajectory, s.coord, s.dir });
    }

    o["samples"] = samples;

    return o;
}

Timetable timetableFromJson(const QJsonObject &o)
{
    Timetable tt;

    tt.valid = true;
    tt.startTime = o["start"].toInt();
    tt.endTrajectory = o["end_traj"].toString();
    tt.endCoord = o["end_coord"].toDouble();
    tt.totalExtraMin = o["total_extra_min"].toDouble();

    for (const QJsonValue &v : o["stations"].toArray())
    {
        const QJsonObject so = v.toObject();
        TimetableStation s;

        s.name = so["name"].toString();
        s.arrival = so["arrival"].toInt();
        s.departure = so["departure"].toInt();
        s.targetTrajectory = so["traj"].toString();
        s.targetCoord = so["coord"].toDouble();
        s.extraMin = so["extra_min"].toDouble();
        s.approachTrajectory = so["approach"].toString();
        s.removalTrajectory = so["removal"].toString();
        s.rightPlatform = so["right"].toInt(-1);
        s.leftPlatform = so["left"].toInt(-1);
        s.visible = so["visible"].toInt(-1);
        tt.stations.push_back(s);
    }

    for (const QJsonValue &v : o["samples"].toArray())
    {
        const QJsonArray a = v.toArray();
        TimetableSample s;

        s.time = a.at(0).toInt();
        s.trajectory = a.at(1).toString();
        s.coord = a.at(2).toDouble();
        s.dir = a.at(3).toInt(1);
        tt.samples.push_back(s);
    }

    return tt;
}

QJsonObject eventToJson(const ScenarioEvent &e)
{
    QJsonObject o;

    o["title"] = e.title;
    o["trigger"] = static_cast<int>(e.trigger);
    o["time"] = e.time;
    o["traj"] = e.traj;
    o["traj2"] = e.traj2;
    o["dir"] = e.dir;
    o["train"] = e.train;
    o["delay"] = e.delaySec;

    QJsonArray actions;

    for (const EventAction &a : e.actions)
    {
        QJsonObject ao;

        ao["type"] = static_cast<int>(a.type);
        ao["a"] = a.a;
        ao["b"] = a.b;
        ao["dir"] = a.dir;
        actions.append(ao);
    }

    o["actions"] = actions;

    return o;
}

ScenarioEvent eventFromJson(const QJsonObject &o)
{
    ScenarioEvent e;

    e.title = o["title"].toString();
    e.trigger = static_cast<TriggerType>(o["trigger"].toInt());
    e.time = o["time"].toInt();
    e.traj = o["traj"].toString();
    e.traj2 = o["traj2"].toString();
    e.dir = o["dir"].toInt(1);
    e.train = o["train"].toString();
    e.delaySec = o["delay"].toInt();

    for (const QJsonValue &v : o["actions"].toArray())
    {
        const QJsonObject ao = v.toObject();
        EventAction a;

        a.type = static_cast<ActionType>(ao["type"].toInt());
        a.a = ao["a"].toString();
        a.b = ao["b"].toString();
        a.dir = ao["dir"].toInt(1);
        e.actions.push_back(a);
    }

    return e;
}

bool fail(QString *error, const QString &text)
{
    if (error != nullptr)
    {
        *error = text;
    }

    return false;
}

QJsonObject readFile(const QString &path, QString *error)
{
    QFile f(path);

    if (!f.open(QIODevice::ReadOnly))
    {
        fail(error, QObject::tr("Не удалось открыть %1: %2").arg(path, f.errorString()));
        return QJsonObject();
    }

    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);

    if (doc.isNull() || !doc.isObject())
    {
        fail(error, QObject::tr("Файл проекта %1 повреждён: %2").arg(path, pe.errorString()));
        return QJsonObject();
    }

    return doc.object();
}

QString resolvePath(const QString &project_path, const QString &rel, const QString &abs)
{
    const QDir base = QFileInfo(project_path).absoluteDir();

    if (!rel.isEmpty())
    {
        const QString p = QDir::cleanPath(base.absoluteFilePath(rel));

        if (QFileInfo::exists(p))
        {
            return p;
        }
    }

    if (!abs.isEmpty() && QFileInfo::exists(abs))
    {
        return abs;
    }

    return QString();
}

} // namespace

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
int suggestStartTime(const Scenario &scenario)
{
    int first = -1;

    for (const PlacedTrain &t : scenario.trains())
    {
        if (t.timetable.valid && (first < 0 || t.timetable.startTime < first))
        {
            first = t.timetable.startTime;
        }
    }

    if (first < 0)
    {
        return 12 * 3600;
    }

    return std::max(0, (first - 180) / 60 * 60);
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
bool saveProject(const QString &path, const ProjectInfo &info, const Scenario &scenario,
                 QString *error)
{
    QJsonObject root;

    root["format"] = QStringLiteral("rrs-scenario-editor-project");
    root["version"] = FORMAT_VERSION;

    const QDir base = QFileInfo(path).absoluteDir();

    root["route_dir"] = info.routeDir;
    root["route_dir_rel"] = info.routeDir.isEmpty() ? QString() : base.relativeFilePath(info.routeDir);
    root["game_root"] = info.gameRoot;
    root["game_root_rel"] = info.gameRoot.isEmpty() ? QString() : base.relativeFilePath(info.gameRoot);

    root["scenario_name"] = info.scenarioName;
    root["description"] = info.description;
    root["start_time"] = info.startTime;

    QJsonArray events;

    for (const ScenarioEvent &e : info.events)
    {
        events.append(eventToJson(e));
    }

    root["events"] = events;
    root["opaque_pre"] = QJsonArray::fromStringList(info.opaquePre);
    root["opaque_post"] = QJsonArray::fromStringList(info.opaquePost);

    QJsonArray trains;

    for (const PlacedTrain &t : scenario.trains())
    {
        QJsonObject o;

        o["name"] = t.name;
        o["lua_var"] = t.luaVar;
        o["title"] = t.title;
        o["source_train"] = t.sourceTrain;
        o["length"] = t.length;
        o["mass"] = t.mass;
        o["autopilot_capable"] = t.autopilotCapable;
        o["trajectory"] = t.trajectory;
        o["coord"] = t.coord;
        o["dir"] = t.dir;
        o["auto"] = t.autoMode;

        QJsonArray groups;

        for (const ConsistGroup &g : t.groups)
        {
            groups.append(groupToJson(g));
        }

        o["groups"] = groups;

        if (t.timetable.valid)
        {
            o["timetable"] = timetableToJson(t.timetable);
        }

        trains.append(o);
    }

    root["trains"] = trains;

    QSaveFile f(path);

    if (!f.open(QIODevice::WriteOnly))
    {
        return fail(error, QObject::tr("Не удалось записать %1: %2").arg(path, f.errorString()));
    }

    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));

    if (!f.commit())
    {
        return fail(error, QObject::tr("Не удалось записать %1: %2").arg(path, f.errorString()));
    }

    return true;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
QString projectRouteDir(const QString &path)
{
    QString err;
    const QJsonObject root = readFile(path, &err);

    return resolvePath(path, root["route_dir_rel"].toString(), root["route_dir"].toString());
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
bool loadProject(const QString &path, ProjectInfo *info, Scenario *scenario, QString *error)
{
    const QJsonObject root = readFile(path, error);

    if (root.isEmpty())
    {
        return false;
    }

    if (root["format"].toString() != QLatin1String("rrs-scenario-editor-project"))
    {
        return fail(error, QObject::tr("Это не файл проекта редактора сценариев."));
    }

    if (root["version"].toInt() > FORMAT_VERSION)
    {
        return fail(error, QObject::tr("Проект создан более новой версией редактора."));
    }

    ProjectInfo pi;

    pi.routeDir = resolvePath(path, root["route_dir_rel"].toString(), root["route_dir"].toString());

    if (pi.routeDir.isEmpty())
    {
        pi.routeDir = root["route_dir"].toString();
    }

    pi.gameRoot = resolvePath(path, root["game_root_rel"].toString(), root["game_root"].toString());
    pi.scenarioName = root["scenario_name"].toString();
    pi.description = root["description"].toString();
    pi.startTime = root["start_time"].toInt(-1);

    for (const QJsonValue &v : root["events"].toArray())
    {
        pi.events.push_back(eventFromJson(v.toObject()));
    }

    for (const QJsonValue &v : root["opaque_pre"].toArray())
    {
        pi.opaquePre << v.toString();
    }

    for (const QJsonValue &v : root["opaque_post"].toArray())
    {
        pi.opaquePost << v.toString();
    }

    Scenario sc;

    for (const QJsonValue &v : root["trains"].toArray())
    {
        const QJsonObject o = v.toObject();
        PlacedTrain t;

        t.name = o["name"].toString();
        t.luaVar = o["lua_var"].toString();
        t.title = o["title"].toString();
        t.sourceTrain = o["source_train"].toString();
        t.length = o["length"].toDouble();
        t.mass = o["mass"].toDouble();
        t.autopilotCapable = o["autopilot_capable"].toBool();
        t.trajectory = o["trajectory"].toString();
        t.coord = o["coord"].toDouble();
        t.dir = o["dir"].toInt(1);
        t.autoMode = o["auto"].toBool();

        for (const QJsonValue &g : o["groups"].toArray())
        {
            t.groups.push_back(groupFromJson(g.toObject()));
        }

        if (o.contains("timetable"))
        {
            t.timetable = timetableFromJson(o["timetable"].toObject());
        }

        sc.add(t);
    }

    if (info != nullptr)
    {
        *info = pi;
    }

    if (scenario != nullptr)
    {
        *scenario = sc;
    }

    return true;
}

} // namespace rse
