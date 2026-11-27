#include    "route/route.h"

#include    <QDir>
#include    <QFile>
#include    <QFileInfo>
#include    <QRegularExpression>
#include    <QXmlStreamReader>

#include    <algorithm>
#include    <array>
#include    <cmath>
#include    <limits>

namespace rse
{

namespace
{

/// Порог совпадения точек траектории, м (так же отбрасывает симулятор)
constexpr double SAME_POINT_EPS = 0.01;

/// Допустимый разрыв между траекториями, сходящимися в одной стрелке, м
constexpr double SWITCH_GAP_WARN = 3.0;

//------------------------------------------------------------------------------
// Прочитать непустые строки текстового файла в UTF-8
//------------------------------------------------------------------------------
bool readTextLines(const QString &path, QStringList &lines)
{
    QFile file(path);

    if (!file.open(QIODevice::ReadOnly))
    {
        return false;
    }

    QString text = QString::fromUtf8(file.readAll());

    if (text.startsWith(QChar(0xFEFF)))
    {
        text.remove(0, 1);
    }

    const QStringList raw = text.split(QLatin1Char('\n'));

    for (const QString &line : raw)
    {
        // trimmed() убирает в том числе '\r' от переводов строк Windows
        const QString s = line.trimmed();

        if (!s.isEmpty())
        {
            lines << s;
        }
    }

    return true;
}

//------------------------------------------------------------------------------
// Разбор одного файла *.traj: строки "x y z пикетаж"
//------------------------------------------------------------------------------
bool parseTrajectory(const QString &path, const QString &name,
                     TrajectoryData &traj, QStringList &warnings)
{
    QStringList lines;

    if (!readTextLines(path, lines))
    {
        warnings << QStringLiteral("Не удалось прочитать траекторию %1").arg(name);
        return false;
    }

    static const QRegularExpression ws(QStringLiteral("\\s+"));

    traj = TrajectoryData();
    traj.name = name;

    double last_z = 0.0;
    int bad_lines = 0;

    for (const QString &line : lines)
    {
        const QStringList parts = line.split(ws, Qt::SkipEmptyParts);

        bool ok_x = false;
        bool ok_y = false;
        bool ok_z = false;
        bool ok_rc = true;

        if (parts.size() < 3)
        {
            ++bad_lines;
            continue;
        }

        const double x = parts[0].toDouble(&ok_x);
        const double y = parts[1].toDouble(&ok_y);
        const double z = parts[2].toDouble(&ok_z);
        const double rc = (parts.size() >= 4) ? parts[3].toDouble(&ok_rc) : 0.0;

        if (!ok_x || !ok_y || !ok_z || !ok_rc)
        {
            ++bad_lines;
            continue;
        }

        const QPointF p(x, y);

        if (traj.points.isEmpty())
        {
            traj.points.push_back(p);
            traj.cumLength.push_back(0.0);
            traj.railwayCoord.push_back(rc);
            traj.heights.push_back(z);
            last_z = z;
            continue;
        }

        const QPointF d = p - traj.points.last();
        const double dz = z - last_z;
        const double dist = std::sqrt(d.x() * d.x() + d.y() * d.y() + dz * dz);

        if (dist <= SAME_POINT_EPS)
        {
            // Сантиметровый отрезок симулятор не создаёт, а переносит начало
            // следующего отрезка в новую точку. Длина не меняется.
            traj.points.last() = p;
            traj.railwayCoord.last() = rc;
            traj.heights.last() = z;
            last_z = z;
            continue;
        }

        traj.points.push_back(p);
        traj.cumLength.push_back(traj.cumLength.last() + dist);
        traj.railwayCoord.push_back(rc);
        traj.heights.push_back(z);
        last_z = z;
    }

    if (bad_lines > 0)
    {
        warnings << QStringLiteral("Траектория %1: пропущено некорректных строк: %2")
                        .arg(name).arg(bad_lines);
    }

    if (!traj.isValid())
    {
        warnings << QStringLiteral("Траектория %1 содержит менее двух различных точек")
                        .arg(name);
        return false;
    }

    traj.length = traj.cumLength.last();

    double min_x = traj.points.first().x();
    double max_x = min_x;
    double min_y = traj.points.first().y();
    double max_y = min_y;

    for (const QPointF &p : std::as_const(traj.points))
    {
        min_x = std::min(min_x, p.x());
        max_x = std::max(max_x, p.x());
        min_y = std::min(min_y, p.y());
        max_y = std::max(max_y, p.y());
    }

    traj.bounds = QRectF(QPointF(min_x, min_y), QPointF(max_x, max_y));

    return true;
}

} // namespace

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void Route::clear()
{
    dir_.clear();
    title_.clear();
    error_.clear();
    warnings_.clear();
    trajectories_.clear();
    switches_.clear();
    stations_.clear();
    waypoints_.clear();
    bounds_ = QRectF();
    endpoint_index_.clear();
    path_dir_.clear();
    path_both_.clear();
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
bool Route::load(const QString &route_dir)
{
    clear();

    const QDir root(route_dir);

    if (!root.exists())
    {
        error_ = QStringLiteral("Каталог не найден: %1").arg(route_dir);
        return false;
    }

    dir_ = root.absolutePath();

    const QDir traj_dir(root.filePath(QStringLiteral("topology/trajectories")));

    if (!traj_dir.exists())
    {
        error_ = QStringLiteral("В каталоге нет topology/trajectories. "
                                "Выберите каталог маршрута (routes/<имя маршрута>).");
        return false;
    }

    const QFileInfoList files = traj_dir.entryInfoList(
        QStringList() << QStringLiteral("*.traj"), QDir::Files, QDir::Name);

    for (const QFileInfo &fi : files)
    {
        TrajectoryData traj;

        if (parseTrajectory(fi.absoluteFilePath(), fi.completeBaseName(),
                            traj, warnings_))
        {
            trajectories_.insert(traj.name, traj);
        }
    }

    if (trajectories_.isEmpty())
    {
        error_ = QStringLiteral("В каталоге topology/trajectories нет ни одной "
                                "корректной траектории (*.traj)");
        return false;
    }

    title_ = root.dirName();

    loadTitle();
    loadSwitches();
    loadStations();
    loadWaypoints();
    loadSpeedMaps();

    resolveSwitches();
    checkWaypoints();
    calcBounds();

    buildIndex();
    calcPathDirections();

    return true;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
const TrajectoryData *Route::find(const QString &name) const
{
    auto it = trajectories_.constFind(name);

    return (it == trajectories_.constEnd()) ? nullptr : &it.value();
}

//------------------------------------------------------------------------------
// Карты скоростей: topology/trajectory-speedmap/*.xml. Файл перечисляет траектории
// (Trajectory/Name) и ограничения (SpeedMap/SpeedLimit = "км/ч от до"), где "от" и
// "до" - пикетаж (-1 и 1e9 - без границы). Пикетаж переводится в координаты
// траектории так же, как в симуляторе (TrajectorySpeedMap::load_config)
//------------------------------------------------------------------------------
void Route::loadSpeedMaps()
{
    const QDir dir(QDir(dir_).filePath(QStringLiteral("topology/trajectory-speedmap")));

    if (!dir.exists())
    {
        return;
    }

    const QFileInfoList files = dir.entryInfoList(QStringList() << QStringLiteral("*.xml"),
                                                  QDir::Files, QDir::Name);

    for (const QFileInfo &fi : files)
    {
        QFile f(fi.absoluteFilePath());

        if (!f.open(QIODevice::ReadOnly))
        {
            warnings_ << QStringLiteral("Не удалось прочитать карту скоростей %1").arg(fi.fileName());
            continue;
        }

        QStringList names;
        QVector<std::array<double, 3>> limits;

        QXmlStreamReader xml(&f);
        QString current;

        while (!xml.atEnd())
        {
            xml.readNext();

            if (xml.isStartElement())
            {
                current = xml.name().toString();

                if (current == QLatin1String("Name"))
                {
                    names << xml.readElementText().trimmed();
                }
                else if (current == QLatin1String("SpeedLimit"))
                {
                    const QStringList t = xml.readElementText().split(QLatin1Char(' '), Qt::SkipEmptyParts);

                    if (!t.isEmpty())
                    {
                        const double kmh = t[0].toDouble();
                        const double c1 = t.size() > 1 ? t[1].toDouble() : -1.0;
                        const double c2 = t.size() > 2 ? t[2].toDouble() : 1e9;

                        limits.push_back({ kmh, std::min(c1, c2), std::max(c1, c2) });
                    }
                }
            }
        }

        if (xml.hasError())
        {
            warnings_ << QStringLiteral("Карта скоростей %1: %2").arg(fi.fileName(), xml.errorString());
            continue;
        }

        for (const QString &name : std::as_const(names))
        {
            auto it = trajectories_.find(name);

            if (it == trajectories_.end() || !it->isValid())
            {
                continue;
            }

            TrajectoryData &t = it.value();

            const double rc_begin = std::min(t.railwayCoord.first(), t.railwayCoord.last());
            const double rc_end = std::max(t.railwayCoord.first(), t.railwayCoord.last());

            for (const auto &lim : std::as_const(limits))
            {
                const double lb = lim[1];
                const double le = lim[2];

                if (!(le > rc_begin && lb < rc_end))
                {
                    continue;
                }

                double begin = 0.0;
                double end = t.length;

                for (int i = 0; i + 1 < t.points.size(); ++i)
                {
                    const double tb = std::min(t.railwayCoord[i], t.railwayCoord[i + 1]);
                    const double te = std::max(t.railwayCoord[i], t.railwayCoord[i + 1]);
                    const double len = t.cumLength[i + 1] - t.cumLength[i];

                    if (lb >= tb && lb < te)
                    {
                        begin = t.cumLength[i] + (lb - tb) / (te - tb) * len;
                    }

                    if (le >= tb && le < te)
                    {
                        end = t.cumLength[i] + (le - tb) / (te - tb) * len;
                    }
                }

                SpeedLimitInterval s;
                s.from = begin;
                s.to = end;
                s.kmh = lim[0];
                t.speedLimits.push_back(s);
            }
        }
    }
}

//------------------------------------------------------------------------------
// Название маршрута из description.xml
//------------------------------------------------------------------------------
void Route::loadTitle()
{
    QFile file(QDir(dir_).filePath(QStringLiteral("description.xml")));

    if (!file.open(QIODevice::ReadOnly))
    {
        return;
    }

    QXmlStreamReader xml(&file);

    while (!xml.atEnd())
    {
        xml.readNext();

        if (xml.isStartElement() && xml.name() == QLatin1String("Title"))
        {
            const QString t = xml.readElementText().trimmed();

            if (!t.isEmpty())
            {
                title_ = t;
            }

            return;
        }
    }
}

//------------------------------------------------------------------------------
// topology/topology.xml: элементы Switch
//------------------------------------------------------------------------------
void Route::loadSwitches()
{
    const QString path = QDir(dir_).filePath(QStringLiteral("topology/topology.xml"));

    QFile file(path);

    if (!file.open(QIODevice::ReadOnly))
    {
        warnings_ << QStringLiteral("Не найден topology/topology.xml: "
                                    "связи между траекториями недоступны");
        return;
    }

    QXmlStreamReader xml(&file);

    SwitchData cur;
    bool in_switch = false;

    while (!xml.atEnd())
    {
        xml.readNext();

        if (xml.isStartElement())
        {
            const QString tag = xml.name().toString();

            if (tag == QLatin1String("Switch"))
            {
                cur = SwitchData();
                in_switch = true;
                continue;
            }

            if (!in_switch)
            {
                continue;
            }

            const QString text = xml.readElementText().trimmed();

            if (tag == QLatin1String("Name"))
                cur.name = text;
            else if (tag == QLatin1String("bwdPlusTraj"))
                cur.bwdPlus = text;
            else if (tag == QLatin1String("bwdMinusTraj"))
                cur.bwdMinus = text;
            else if (tag == QLatin1String("fwdPlusTraj"))
                cur.fwdPlus = text;
            else if (tag == QLatin1String("fwdMinusTraj"))
                cur.fwdMinus = text;
            else if (tag == QLatin1String("fwdMinusTrajReversed"))
                cur.fwdMinusReversed = text;
            else if (tag == QLatin1String("SignalLiterFwd"))
                cur.signalFwd = text;
            else if (tag == QLatin1String("SignalLiterBwd"))
                cur.signalBwd = text;
            else if (tag == QLatin1String("SignalModelFwd"))
                cur.modelFwd = text;
            else if (tag == QLatin1String("SignalModelBwd"))
                cur.modelBwd = text;
        }
        else if (xml.isEndElement() && xml.name() == QLatin1String("Switch"))
        {
            if (in_switch)
            {
                switches_.push_back(cur);
            }

            in_switch = false;
        }
    }

    if (xml.hasError())
    {
        warnings_ << QStringLiteral("Ошибка разбора topology.xml: %1 (строка %2)")
                         .arg(xml.errorString()).arg(xml.lineNumber());
    }
}

//------------------------------------------------------------------------------
// topology/stations.conf: "имя<TAB>x<TAB>y<TAB>z"
//------------------------------------------------------------------------------
void Route::loadStations()
{
    QStringList lines;

    if (!readTextLines(QDir(dir_).filePath(QStringLiteral("topology/stations.conf")), lines))
    {
        return;
    }

    for (const QString &line : std::as_const(lines))
    {
        const QStringList parts = line.split(QLatin1Char('\t'), Qt::SkipEmptyParts);

        bool ok_x = false;
        bool ok_y = false;

        if (parts.size() < 3)
        {
            continue;
        }

        const double x = parts[1].toDouble(&ok_x);
        const double y = parts[2].toDouble(&ok_y);

        if (!ok_x || !ok_y)
        {
            continue;
        }

        StationData st;
        st.name = parts[0].trimmed();
        st.pos = QPointF(x, y);

        stations_.push_back(st);
    }
}

//------------------------------------------------------------------------------
// topology/waypoints.conf: "название<TAB>траектория<TAB>dir<TAB>coord<TAB>..."
//------------------------------------------------------------------------------
void Route::loadWaypoints()
{
    QStringList lines;

    if (!readTextLines(QDir(dir_).filePath(QStringLiteral("topology/waypoints.conf")), lines))
    {
        return;
    }

    for (const QString &line : std::as_const(lines))
    {
        const QStringList parts = line.split(QLatin1Char('\t'), Qt::SkipEmptyParts);

        bool ok_dir = false;
        bool ok_coord = false;

        if (parts.size() < 4)
        {
            continue;
        }

        WaypointData wp;
        wp.title = parts[0].trimmed();
        wp.trajectory = parts[1].trimmed();
        wp.dir = parts[2].toInt(&ok_dir);
        wp.coord = parts[3].toDouble(&ok_coord);

        if (ok_dir && ok_coord)
        {
            waypoints_.push_back(wp);
        }
    }
}

//------------------------------------------------------------------------------
// Положение узла стрелки: все примыкающие траектории своими концами
// (или началами) должны сходиться в одной точке
//------------------------------------------------------------------------------
void Route::resolveSwitches()
{
    for (SwitchData &sw : switches_)
    {
        struct Attach
        {
            const QString &traj_name;
            bool at_end;
        };

        const Attach attaches[] = {
            { sw.bwdPlus, true },
            { sw.bwdMinus, true },
            { sw.fwdMinusReversed, true },
            { sw.fwdPlus, false },
            { sw.fwdMinus, false },
        };

        QVector<QPointF> pts;

        for (const Attach &a : attaches)
        {
            if (a.traj_name.isEmpty())
            {
                continue;
            }

            const TrajectoryData *traj = find(a.traj_name);

            if (traj == nullptr)
            {
                warnings_ << QStringLiteral("Стрелка %1: траектория %2 не найдена")
                                 .arg(sw.name, a.traj_name);
                continue;
            }

            pts.push_back(a.at_end ? traj->points.last() : traj->points.first());
        }

        if (pts.isEmpty())
        {
            continue;
        }

        sw.pos = pts.first();
        sw.hasPos = true;

        double max_gap = 0.0;

        for (int i = 1; i < pts.size(); ++i)
        {
            const QPointF d = pts[i] - pts[0];
            max_gap = std::max(max_gap, std::hypot(d.x(), d.y()));
        }

        if (max_gap > SWITCH_GAP_WARN)
        {
            warnings_ << QStringLiteral("Стрелка %1: разрыв между примыкающими "
                                        "траекториями %2 м")
                             .arg(sw.name).arg(max_gap, 0, 'f', 1);
        }
    }
}

//------------------------------------------------------------------------------
// Путевая точка с несуществующей траекторией приведёт к ошибке при старте
// сценария, поэтому сообщаем о ней заранее
//------------------------------------------------------------------------------
void Route::checkWaypoints()
{
    for (const WaypointData &wp : std::as_const(waypoints_))
    {
        if (find(wp.trajectory) == nullptr)
        {
            warnings_ << QStringLiteral("Путевая точка \"%1\": траектория %2 не найдена")
                             .arg(wp.title, wp.trajectory);
        }
    }
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void Route::calcBounds()
{
    bool first = true;
    double min_x = 0.0;
    double max_x = 0.0;
    double min_y = 0.0;
    double max_y = 0.0;

    for (const TrajectoryData &traj : std::as_const(trajectories_))
    {
        const QRectF &b = traj.bounds;

        if (first)
        {
            min_x = b.left();
            max_x = b.right();
            min_y = b.top();
            max_y = b.bottom();
            first = false;
            continue;
        }

        min_x = std::min(min_x, b.left());
        max_x = std::max(max_x, b.right());
        min_y = std::min(min_y, b.top());
        max_y = std::max(max_y, b.bottom());
    }

    bounds_ = QRectF(QPointF(min_x, min_y), QPointF(max_x, max_y));
}

//------------------------------------------------------------------------------
// Что за концом траектории. Каждая траектория занимает в стрелке одно место:
// bwd* - своим концом, fwdPlus/fwdMinus - началом, fwdMinusReversed - концом.
// Проезд идёт на противоположную сторону стрелки; "плюс" соединяется с "плюсом"
//------------------------------------------------------------------------------
QVector<Route::Endpoint> Route::crossings(const QString &trajectory, bool at_end) const
{
    QVector<Endpoint> result;

    const QVector<int> candidates = endpoint_index_.value(endKey(trajectory, at_end));

    for (int sw_index : candidates)
    {
        const SwitchData &sw = switches_[sw_index];

        // Где траектория стоит в этой стрелке на нужном конце
        bool on_bwd = false;
        bool is_plus = true;
        bool found = false;

        if (at_end)
        {
            if (sw.bwdPlus == trajectory)
            {
                found = on_bwd = true;
                is_plus = true;
            }
            else if (sw.bwdMinus == trajectory)
            {
                found = on_bwd = true;
                is_plus = false;
            }
            else if (sw.fwdMinusReversed == trajectory)
            {
                found = true;
                on_bwd = false;
                is_plus = false;
            }
        }
        else
        {
            if (sw.fwdPlus == trajectory)
            {
                found = true;
                on_bwd = false;
                is_plus = true;
            }
            else if (sw.fwdMinus == trajectory)
            {
                found = true;
                on_bwd = false;
                is_plus = false;
            }
        }

        if (!found)
        {
            continue;
        }

        // Противоположная сторона стрелки
        QVector<Endpoint> others;

        auto add = [&](const QString &name, bool end, bool plus)
        {
            if (!name.isEmpty() && trajectories_.contains(name))
            {
                Endpoint e;
                e.trajectory = name;
                e.atEnd = end;
                e.plus = plus;
                e.switchName = sw.name;
                others.push_back(e);
            }
        };

        int same_side = 0;

        if (on_bwd)
        {
            add(sw.fwdPlus, false, true);
            add(sw.fwdMinus, false, false);
            add(sw.fwdMinusReversed, true, false);

            same_side = (sw.bwdPlus.isEmpty() ? 0 : 1) + (sw.bwdMinus.isEmpty() ? 0 : 1);
        }
        else
        {
            add(sw.bwdPlus, true, true);
            add(sw.bwdMinus, true, false);

            same_side = (sw.fwdPlus.isEmpty() ? 0 : 1)
                      + ((sw.fwdMinus.isEmpty() && sw.fwdMinusReversed.isEmpty()) ? 0 : 1);
        }

        if (others.isEmpty())
        {
            continue;
        }

        // На стороне въезда две траектории: «плюс» идёт в «плюс», «минус» в «минус»
        if (same_side >= 2)
        {
            for (const Endpoint &e : std::as_const(others))
            {
                if (e.plus == is_plus)
                {
                    result.push_back(e);
                    break;
                }
            }

            if (!result.isEmpty() && result.last().switchName == sw.name)
            {
                continue;
            }
        }

        for (const Endpoint &e : std::as_const(others))
        {
            result.push_back(e);
        }
    }

    return result;
}

//------------------------------------------------------------------------------
// Ключ конца траектории для индекса переходов
//------------------------------------------------------------------------------
QString Route::endKey(const QString &trajectory, bool at_end)
{
    return trajectory + (at_end ? QStringLiteral("\x01" "e") : QStringLiteral("\x01" "s"));
}

//------------------------------------------------------------------------------
// Индекс концов траекторий по стрелкам (быстрый поиск переходов)
//------------------------------------------------------------------------------
void Route::buildIndex()
{
    endpoint_index_.clear();

    for (int i = 0; i < switches_.size(); ++i)
    {
        const SwitchData &sw = switches_[i];

        auto add = [&](const QString &name, bool end)
        {
            if (!name.isEmpty() && trajectories_.contains(name))
            {
                endpoint_index_[endKey(name, end)].push_back(i);
            }
        };

        add(sw.bwdPlus, true);
        add(sw.bwdMinus, true);
        add(sw.fwdMinusReversed, true);
        add(sw.fwdPlus, false);
        add(sw.fwdMinus, false);
    }
}

//------------------------------------------------------------------------------
// Маневровый светофор: литера начинается с М (лат. или кирилл.), либо с Н/Ч и
// затем М (НМ3, ЧМ5)
//------------------------------------------------------------------------------
static bool isShuntingLiter(const QString &liter)
{
    const QString l = liter.trimmed().toUpper();

    auto is_m = [](QChar c) { return c == QChar('M') || c == QChar(0x041C); };

    if (l.isEmpty())
    {
        return false;
    }

    if (is_m(l[0]))
    {
        return true;
    }

    return l.size() > 1 && (l[0] == QChar(0x041D) || l[0] == QChar(0x0427)) && is_m(l[1]);
}

//------------------------------------------------------------------------------
// Пара параллельных путей: распознавание двухпутного перегона и вывод
// направлений по правилу правостороннего движения РЖД (поезда идут по правому
// пути, встречный путь слева; ПТЭ: «правильный путь», ИДП гл. II).
//
// На двухпутном перегоне у каждого пути светофоры своего направления, но часто
// есть и встречные (пропуск по неправильному пути, двусторонняя автоблокировка
// однопутных вставок) - литеры дают конфликт ±1. Пара путей решает его
// геометрией: у соседних траекторий (межосевое 3.2-7.5 м, почти параллельных,
// близкой длины) направления противоположны, и правильное направление пути -
// то, при котором сосед оказывается слева. Пара отбрасывается целиком, если
// однозначный светофор любого члена ей противоречит: соседние станционные
// пути бывают одного направления (чётные приёмо-отправочные рядом) и правило
// правостороннего движения к ним неприменимо
//------------------------------------------------------------------------------
namespace
{

constexpr double PAIR_MIN_LENGTH = 80.0;  // короче - горловина, не перегон, м
constexpr double PAIR_GAP        = 12.0;  // запас габаритов при поиске кандидатов, м
constexpr double PAIR_DIST_MIN   = 3.2;   // межосевое двухпутной линии >= 4.1 м (ПТЭ), м
constexpr double PAIR_DIST_MAX   = 7.5;   // шире - разные станционные пути, м
constexpr double PAIR_TOUCH_MAX  = 15.0;  // сосед в сечении не дальше этого, м
constexpr double PAIR_SIDE_MIN   = 1.5;   // минимальное боковое смещение соседа, м
constexpr double PAIR_COS        = 0.95;  // предел «почти параллельны»
constexpr double PAIR_LEN_TOL    = 0.4;   // допускаемая разность длин пары, доля
constexpr double PAIR_STEP       = 25.0;  // шаг сэмплирования полилинии, м

// Голоса стрелок направлению траектории: [0] - «туда» (+1), [1] - «обратно» (-1)
struct DirVotes
{
    int normal[2] = { 0, 0 };
    int wrong[2] = { 0, 0 };
};

double dot2(const QPointF &a, const QPointF &b)
{
    return a.x() * b.x() + a.y() * b.y();
}

// Расстояние от точки до отрезка, м
double segPointDistance(const QPointF &p, const QPointF &a, const QPointF &b)
{
    const QPointF ab = b - a;
    const double denom = dot2(ab, ab);

    double t = 0.0;

    if (denom > 0.0)
    {
        t = std::clamp(dot2(p - a, ab) / denom, 0.0, 1.0);
    }

    const QPointF q = a + ab * t;

    return std::hypot(p.x() - q.x(), p.y() - q.y());
}

// Расстояние до ближайшей точки полилинии; q - проекция (может быть nullptr)
double nearestOnPolyline(const QPointF &p, const QVector<QPointF> &points, QPointF *q)
{
    double best = std::numeric_limits<double>::max();

    for (int i = 0; i + 1 < points.size(); ++i)
    {
        const QPointF ab = points[i + 1] - points[i];
        const double denom = dot2(ab, ab);

        double t = 0.0;

        if (denom > 0.0)
        {
            t = std::clamp(dot2(p - points[i], ab) / denom, 0.0, 1.0);
        }

        const QPointF proj = points[i] + ab * t;
        const double d = std::hypot(p.x() - proj.x(), p.y() - proj.y());

        if (d < best)
        {
            best = d;

            if (q != nullptr)
            {
                *q = proj;
            }
        }
    }

    return best;
}

// Минимальное межосевое расстояние полилиний (первая сэмплируется через шаг)
double polylineDistance(const TrajectoryData &a, const TrajectoryData &b)
{
    double best = std::numeric_limits<double>::max();

    for (int i = 0; i + 1 < a.points.size(); ++i)
    {
        const QPointF &p0 = a.points[i];
        const QPointF &p1 = a.points[i + 1];
        const double seg = std::hypot(p1.x() - p0.x(), p1.y() - p0.y());
        const int n = std::max(static_cast<int>(seg / PAIR_STEP), 1);

        for (int k = 0; k < n; ++k)
        {
            const double t = static_cast<double>(k) / n;
            const QPointF p(p0.x() + (p1.x() - p0.x()) * t,
                            p0.y() + (p1.y() - p0.y()) * t);

            const double d = nearestOnPolyline(p, b.points, nullptr);

            if (d < best)
            {
                best = d;
            }
        }
    }

    return best;
}

// Правостороннее правило для пары: направление пути a, при котором путь b
// оказывается слева (встречные слева), есть его правильное направление.
// Сторона определяется в трёх сечениях (30/50/70% длины) по медиане
bool pairSideDirection(const TrajectoryData &a, const TrajectoryData &b, int &dir_a)
{
    std::array<double, 3> sides = { 0.0, 0.0, 0.0 };
    int taken = 0;

    for (double frac : { 0.3, 0.5, 0.7 })
    {
        QPointF tangent;

        const QPointF p = a.pointAt(frac * a.length, &tangent);

        QPointF q;

        if (nearestOnPolyline(p, b.points, &q) > PAIR_TOUCH_MAX)
        {
            return false;
        }

        sides[taken++] = dot2(q - p, QPointF(-tangent.y(), tangent.x()));
    }

    std::sort(sides.begin(), sides.end());

    const double side = sides[1];

    if (std::abs(side) < PAIR_SIDE_MIN)
    {
        return false;
    }

    dir_a = (side > 0.0) ? 1 : -1;

    return true;
}

// Направления по парам параллельных путей. Для траектории возвращаются только
// при согласии всех её принятых пар; конфликт оставляет траекторию без ответа
QHash<QString, int> pairDirections(const QMap<QString, TrajectoryData> &trajectories,
                                   const QHash<QString, DirVotes> &votes)
{
    QHash<QString, int> result;

    // Однозначный светофор члена - арбитр: пара, ему противоречащая, отбрасывается
    auto signalDir = [&votes](const QString &name) -> int
    {
        const auto it = votes.constFind(name);

        if (it == votes.constEnd())
        {
            return 0;
        }

        const DirVotes &v = it.value();

        if (v.normal[0] > 0 && v.normal[1] == 0)
        {
            return 1;
        }

        if (v.normal[1] > 0 && v.normal[0] == 0)
        {
            return -1;
        }

        if (v.normal[0] == 0 && v.normal[1] == 0)
        {
            // Только «неправильные» светофоры: правильное направление обратное
            if (v.wrong[0] > 0 && v.wrong[1] == 0)
            {
                return -1;
            }

            if (v.wrong[1] > 0 && v.wrong[0] == 0)
            {
                return 1;
            }
        }

        return 0;
    };

    QVector<const TrajectoryData *> longs;

    longs.reserve(trajectories.size());

    for (auto it = trajectories.constBegin(); it != trajectories.constEnd(); ++it)
    {
        if (it->length > PAIR_MIN_LENGTH)
        {
            longs.append(&it.value());
        }
    }

    QHash<QString, std::array<int, 2>> pair_votes;

    for (int i = 0; i < longs.size(); ++i)
    {
        const TrajectoryData &a = *longs[i];

        for (int j = i + 1; j < longs.size(); ++j)
        {
            const TrajectoryData &b = *longs[j];

            // QRectF::intersects считает вырожденный габарит (прямой участок
            // даёт нулевую высоту) непересекающимся ни с чем, поэтому
            // перекрытие проверяется по диапазонам координат явно
            const QRectF ga = a.bounds.adjusted(-PAIR_GAP, -PAIR_GAP, PAIR_GAP, PAIR_GAP);
            const QRectF gb = b.bounds.adjusted(-PAIR_GAP, -PAIR_GAP, PAIR_GAP, PAIR_GAP);

            if (ga.right() < gb.left() || gb.right() < ga.left()
                || ga.bottom() < gb.top() || gb.bottom() < ga.top())
            {
                continue;
            }

            if (std::abs(a.length - b.length) > PAIR_LEN_TOL * std::max(a.length, b.length))
            {
                continue;
            }

            const QPointF da = a.points.last() - a.points.first();
            const QPointF db = b.points.last() - b.points.first();
            const double la = std::hypot(da.x(), da.y());
            const double lb = std::hypot(db.x(), db.y());

            if (la <= 0.0 || lb <= 0.0)
            {
                continue;
            }

            if (std::abs(dot2(da, db)) / (la * lb) < PAIR_COS)
            {
                continue;
            }

            const double dist = polylineDistance(a, b);

            if (dist < PAIR_DIST_MIN || dist > PAIR_DIST_MAX)
            {
                continue;
            }

            int dir_a = 0;

            if (!pairSideDirection(a, b, dir_a))
            {
                continue;
            }

            // Однозначный светофор члена - арбитр: пара, ему противоречащая,
            // отбрасывается целиком
            const int sa = signalDir(a.name);
            const int sb = signalDir(b.name);

            if ((sa != 0 && sa != dir_a) || (sb != 0 && sb != -dir_a))
            {
                continue;
            }

            pair_votes[a.name][dir_a > 0 ? 0 : 1] += 1;
            pair_votes[b.name][(-dir_a) > 0 ? 0 : 1] += 1;
        }
    }

    for (auto it = pair_votes.constBegin(); it != pair_votes.constEnd(); ++it)
    {
        if ((*it)[0] > 0 && (*it)[1] == 0)
        {
            result.insert(it.key(), 1);
        }
        else if ((*it)[1] > 0 && (*it)[0] == 0)
        {
            result.insert(it.key(), -1);
        }
    }

    return result;
}

} // namespace

//------------------------------------------------------------------------------
// Правильное направление путей по светофорам и геометрии.
//
// Соглашение симулятора (v1.9.4, Topology::open_route_signals): для поезда,
// движущегося в направлении +1 по траектории, у стрелки впереди открывается
// светофор signalFwd (SignalLiterFwd); signalBwd (SignalLiterBwd) открывается
// для движения -1. Светофор Fwd стоит на стороне "назад" стрелки и ведёт
// поезда в сторону "вперёд" (+1 через стрелку); светофор Bwd - со стороны
// "вперёд", ведущий в сторону "назад" (-1). Для траектории светофор означает
// направление движения вдоль неё, зависящее от примыкания к стрелке:
//   bwd*, концом               : +1 через стрелку = уход с траектории (+1)
//   fwdPlus/fwdMinus, началом  : +1 через стрелку = въезд на траекторию (+1)
//   fwdMinusReversed, концом   : +1 через стрелку = въезд с конца (-1)
// Светофоры с обеими литерами (проходные на перегоне, у них FWD и BWD совпадают)
// направления не задают. Светофор с буквой "Д" в конце (ЧД, НД) - выход на
// неправильный путь, он направление не подтверждает, а наоборот указывает
// неправильное (правильное - противоположное).
//
// Направление выводится по уровням, каждый следующий заполняет то, что не
// заполнено предыдущим:
//   1) литеры стрелок (см. выше) с переносом по цепочке траекторий;
//   2) пары параллельных путей - правостороннее движение РЖД (см. выше),
//      арбитр - однозначные литеры членов пары;
//   3) конфликт литер (светофоры в обе стороны), не разрешённый парой, -
//      двусторонний путь (path_both_): по однопутным перегонам с двусторонней
//      автоблокировкой и станционным путям приёма/отправления поезда ходят
//      в обе стороны, и назначать им одно направление нельзя
//------------------------------------------------------------------------------
void Route::calcPathDirections()
{
    path_dir_.clear();
    path_both_.clear();

    QHash<QString, DirVotes> votes;

    for (const SwitchData &sw : std::as_const(switches_))
    {
        const bool fwd = !sw.signalFwd.isEmpty();
        const bool bwd = !sw.signalBwd.isEmpty();

        if (fwd == bwd)
        {
            continue;
        }

        const QString liter = bwd ? sw.signalBwd : sw.signalFwd;

        // Маневровые светофоры (М7, НМ3, ЧМ5) направление поездов не задают
        if (isShuntingLiter(liter))
        {
            continue;
        }

        const bool wrong = liter.trimmed().toUpper().endsWith(QChar(0x0414));

        // Движение, которое ведёт светофор: литера Fwd - +1 вперёд через стрелку,
        // литера Bwd - -1 назад (как в open_route_signals симулятора)
        const int through = bwd ? -1 : 1;

        auto vote = [&](const QString &name, int sign)
        {
            if (name.isEmpty() || !trajectories_.contains(name))
            {
                return;
            }

            DirVotes &v = votes[name];
            (wrong ? v.wrong : v.normal)[sign > 0 ? 0 : 1] += 1;
        };

        vote(sw.bwdPlus, through);
        vote(sw.bwdMinus, through);
        vote(sw.fwdPlus, through);
        vote(sw.fwdMinus, through);
        vote(sw.fwdMinusReversed, -through);
    }

    for (auto it = votes.constBegin(); it != votes.constEnd(); ++it)
    {
        const DirVotes &v = it.value();

        int dir = 0;

        if (v.normal[0] > 0 && v.normal[1] == 0)
        {
            dir = 1;
        }
        else if (v.normal[1] > 0 && v.normal[0] == 0)
        {
            dir = -1;
        }
        else if (v.normal[0] == 0 && v.normal[1] == 0)
        {
            // Только "неправильные" светофоры: правильное направление обратное
            if (v.wrong[0] > 0 && v.wrong[1] == 0)
            {
                dir = -1;
            }
            else if (v.wrong[1] > 0 && v.wrong[0] == 0)
            {
                dir = 1;
            }
        }

        if (dir != 0)
        {
            path_dir_.insert(it.key(), dir);
        }
    }

    // Уровень 2: пары параллельных путей - правостороннее движение РЖД.
    // Решают конфликт литер на двухпутных участках и направляют пути без
    // однозначных светофоров; пути с уровнем 1 не трогаются
    const QHash<QString, int> pair_dir = pairDirections(trajectories_, votes);

    for (auto it = pair_dir.constBegin(); it != pair_dir.constEnd(); ++it)
    {
        if (!path_dir_.contains(it.key()))
        {
            path_dir_.insert(it.key(), it.value());
        }
    }

    // Траектория с конфликтными голосами (±1) направления по переносу не
    // получает: по ней ходят в обе стороны, и перенесённое с одной стороны
    // направление было бы случайным (зависело бы от порядка переноса)
    auto isConflicting = [&votes](const QString &name) -> bool
    {
        const auto it = votes.constFind(name);

        return it != votes.constEnd() && it->normal[0] > 0 && it->normal[1] > 0;
    };

    // Уровень 3: перенос по простым стыкам до установления
    bool changed = true;

    while (changed)
    {
        changed = false;

        for (const SwitchData &sw : std::as_const(switches_))
        {
            const bool plain = !sw.bwdPlus.isEmpty() && sw.bwdMinus.isEmpty()
                            && !sw.fwdPlus.isEmpty() && sw.fwdMinus.isEmpty()
                            && sw.fwdMinusReversed.isEmpty();

            if (!plain || !trajectories_.contains(sw.bwdPlus) || !trajectories_.contains(sw.fwdPlus))
            {
                continue;
            }

            const int a = path_dir_.value(sw.bwdPlus, 0);
            const int b = path_dir_.value(sw.fwdPlus, 0);

            if (a != 0 && b == 0 && !isConflicting(sw.fwdPlus))
            {
                path_dir_.insert(sw.fwdPlus, a);
                changed = true;
            }
            else if (b != 0 && a == 0 && !isConflicting(sw.bwdPlus))
            {
                path_dir_.insert(sw.bwdPlus, b);
                changed = true;
            }
        }
    }

    // Уровень 4: конфликт литер (светофоры в обе стороны), не разрешённый
    // парой соседнего пути, - двусторонний путь
    for (auto it = votes.constBegin(); it != votes.constEnd(); ++it)
    {
        if (!path_dir_.contains(it.key()) && it->normal[0] > 0 && it->normal[1] > 0)
        {
            path_both_.insert(it.key());
        }
    }
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
const SwitchData *Route::findSwitch(const QString &name) const
{
    if (switch_by_name_.size() != switches_.size())
    {
        switch_by_name_.clear();

        for (int i = 0; i < switches_.size(); ++i)
        {
            switch_by_name_.insert(switches_[i].name, i);
        }
    }

    const int i = switch_by_name_.value(name, -1);

    return (i >= 0) ? &switches_[i] : nullptr;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
SignalRole signalRoleOf(const QString &model)
{
    const QString tail = model.right(4);

    if (tail == QLatin1String("line"))
        return SignalRole::Line;

    if (tail == QLatin1String("entr"))
        return SignalRole::Enter;

    if (tail == QLatin1String("rout"))
        return SignalRole::Route;

    if (tail == QLatin1String("exit"))
        return SignalRole::Exit;

    if (tail == QLatin1String("shnt"))
        return SignalRole::Shunt;

    return SignalRole::None;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
int Route::orientation(const QString &trajectory) const
{
    return path_dir_.value(trajectory, 0);
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
PathDirection Route::pathDirection(const QString &trajectory) const
{
    const int o = path_dir_.value(trajectory, 0);

    if (o > 0)
    {
        return PathDirection::Fwd;
    }

    if (o < 0)
    {
        return PathDirection::Bwd;
    }

    if (path_both_.contains(trajectory))
    {
        return PathDirection::Both;
    }

    return PathDirection::Unknown;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
Route::Hit Route::nearest(const QPointF &world, double max_distance) const
{
    Hit best;
    best.distance = max_distance;

    for (const TrajectoryData &traj : trajectories_)
    {
        // Быстрый отсев по габариту
        if (!traj.bounds.adjusted(-max_distance, -max_distance,
                                  max_distance, max_distance).contains(world))
        {
            continue;
        }

        for (int i = 0; i + 1 < traj.points.size(); ++i)
        {
            const QPointF a = traj.points[i];
            const QPointF ab = traj.points[i + 1] - a;
            const double len2 = QPointF::dotProduct(ab, ab);

            double t = 0.0;

            if (len2 > 0.0)
            {
                t = std::clamp(QPointF::dotProduct(world - a, ab) / len2, 0.0, 1.0);
            }

            const QPointF q = a + ab * t;
            const QPointF dq = world - q;
            const double dist = std::hypot(dq.x(), dq.y());

            if (dist < best.distance || (!best.valid && dist <= best.distance))
            {
                best.valid = true;
                best.trajectory = traj.name;
                best.distance = dist;
                best.pos = q;
                best.coord = traj.cumLength[i]
                           + t * (traj.cumLength[i + 1] - traj.cumLength[i]);
            }
        }
    }

    return best;
}

} // namespace rse
