#ifndef     RSE_STATIONROUTE_H
#define     RSE_STATIONROUTE_H

#include    "scenario/pathfinder.h"

#include    <QString>
#include    <QStringList>
#include    <QVector>

namespace rse
{

//------------------------------------------------------------------------------
// Правила построения маршрутов графика, выведенные из исходников симулятора и
// готовых сценариев игры.
//
// Автопилот поезда с графиком сам строит маршруты, но только по полям графика:
//  - когда поезд вступает на ApproachTraj станции, строится маршрут от неё до
//    TargetTraj (путь станции) - открывается входной светофор;
//  - при отправлении строится маршрут от TargetTraj до RemovalTraj - открывается
//    выходной;
//  - направление поиска маршрута одно на весь график (проверяется "туда", затем
//    "обратно" от текущей траектории до конечной станции), а сам маршрут ищется
//    поиском в ширину - по наименьшему ЧИСЛУ траекторий, а не по длине.
// Поэтому подход, уход и путь станции надо выбирать так, чтобы маршрут игры
// совпал с задуманным путём, а все траектории шли в общем направлении
//------------------------------------------------------------------------------

/// Траектория станции или перегона (по имени, как в маршрутах игры:
/// «track_<станция>_…» - станция, «track_<А>-<Б>_…» и «routeN_…» - перегон)
enum class TrajKind
{
    Station,
    Haul
};

TrajKind trajectoryKind(const QString &name);

/// Светофор в конце траектории по ходу движения
struct EndSignal
{
    SignalRole role = SignalRole::None;
    QString liter;

    /// Стрелка, несущая светофор (для поиска места на карте; пусто - светофора нет)
    QString sw;
};

EndSignal signalAtEnd(const Route &route, const QString &trajectory, int dir);

/// Шаг пути: траектория и направление движения по ней
struct PathStep
{
    QString trajectory;
    int dir = 1;
    double length = 0.0;
};

QVector<PathStep> stepsOf(const Route &route, const PathResult &path);

/// Станционная зона вдоль пути: подряд идущие траектории станции
struct StationZone
{
    int first = 0;
    int last = 0;

    /// Шаг, в конце которого входной светофор (-1 - нет)
    int enterStep = -1;

    /// Последний шаг с выходным светофором (-1 - нет)
    int exitStep = -1;

    /// Короткое имя станции из названий траекторий («bat»)
    QString token;

    /// Предлагаемые подход и уход (индексы шагов, -1 - нет)
    int approachStep = -1;
    int removalStep = -1;

    bool signalled() const
    {
        return enterStep >= 0 || exitStep >= 0;
    }
};

QVector<StationZone> detectZones(const Route &route, const QVector<PathStep> &steps);

//------------------------------------------------------------------------------
// Проверка графика так, как его выполнит автопилот
//------------------------------------------------------------------------------
struct AutoStation
{
    QString name;
    QString target;
    QString approach;
    QString removal;
};

/// Светофор, который откроется маршрутом: место на карте и литера
struct RouteSignalMark
{
    /// Стрелка, на которой стоит светофор
    QString sw;

    /// Траектория, на конце которой он стоит (по ходу движения)
    QString traj;

    QString liter;

    SignalRole role = SignalRole::None;

    /// Станционный (входной, маршрутный, выходной), а не линейный
    bool station = true;
};

struct StationRouteCheck
{
    QString station;

    bool arrivalOk = true;
    bool departureOk = true;

    /// Сколько траекторий в маршруте, и какие станционные светофоры он откроет
    int arrivalHops = 0;
    int departureHops = 0;
    QStringList arrivalSignals;
    QStringList departureSignals;

    /// Светофоры маршрутов приёма и отправления (для подсветки на карте)
    QVector<RouteSignalMark> arrivalMarks;
    QVector<RouteSignalMark> departureMarks;

    QStringList problems;
};

struct AutopilotReport
{
    /// Направление поиска маршрутов на весь график; 0 - график невыполним
    int dir = 0;

    QVector<StationRouteCheck> rows;

    /// Траектории по порядку так, как поведёт игра
    QStringList chain;

    /// Общие замечания (недостижимая конечная точка, развилки на перегонах)
    QStringList problems;

    bool ok() const;
};

/// Выполнить график "на бумаге". Первая станция - место стоянки поезда
AutopilotReport simulateAutopilot(const Route &route, const QVector<AutoStation> &stations);

/// Есть ли на пути между шагами from..to стрелки с ответвлением (их положение маршрутом
/// не задаётся, если путь не входит ни в один маршрут)
bool haulHasSwitches(const Route &route, const QVector<PathStep> &steps, int from, int to);

/// Первое место, где путь игры расходится с задуманным (-1 - не расходится)
int firstDivergence(const QStringList &chain, const QVector<PathStep> &intended);

//------------------------------------------------------------------------------
// Пути станции, на которые можно принять поезд
//------------------------------------------------------------------------------
struct StationTrack
{
    QString trajectory;
    double coord = 0.0;
    double distance = 0.0;
};

/// Станционные траектории вблизи станции с ближайшей к ней точкой, по удалению
QVector<StationTrack> stationTracks(const Route &route, const QPointF &stationPos,
                                    double radius = 150.0);

} // namespace rse

#endif // RSE_STATIONROUTE_H
