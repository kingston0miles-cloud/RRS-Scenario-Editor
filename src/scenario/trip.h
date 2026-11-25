#ifndef     RSE_TRIP_H
#define     RSE_TRIP_H

#include    "scenario/dynamics.h"
#include    "scenario/pathfinder.h"

#include    <QString>
#include    <QStringList>
#include    <QVector>

namespace rse
{

//------------------------------------------------------------------------------
// Станция, через которую проходит путь
//------------------------------------------------------------------------------
struct TripStation
{
    QString name;

    /// Расстояние от начала пути до ближайшей к станции точки пути, м
    double s = 0.0;

    /// Расстояние от точки станции на карте до пути, м
    double offset = 0.0;
};

//------------------------------------------------------------------------------
// Положение головы поезда на пути
//------------------------------------------------------------------------------
struct TripPosition
{
    QString trajectory;
    double coord = 0.0;
    int dir = 1;
};

//------------------------------------------------------------------------------
// Путь поезда от его места до выбранной точки и всё, что нужно для расчёта хода
//------------------------------------------------------------------------------
struct TripPlan
{
    bool ok = false;
    QString error;

    PathResult path;

    /// Длина пути, м
    double length = 0.0;

    /// Станции вдоль пути по порядку
    QVector<TripStation> stations;

    TrackProfile profile;
    TrainDynamics dynamics;

    /// Что мешает или может помешать: занятые пути, нестандартные стрелки
    QStringList warnings;

    /// Положение головы на расстоянии s от начала пути
    TripPosition position(double s) const;
};

/// Точка, через которую путь должен пройти (выбранный путь станции)
struct TripVia
{
    QString trajectory;
    double coord = 0.0;
};

/// Точка пути для закреплённой станции: выбранный путь (track, trackCoord) или
/// ближайший к станции путь. false - станция не найдена или путей у неё нет
bool pinToPoint(const Route &route, const QString &station, const QString &track,
                double trackCoord, TripVia *via);

/// Подготовить путь поезда с индексом train_index до точки (traj, coord),
/// проходящий по порядку через точки vias
TripPlan buildTripPlan(const Route &route, const VehicleCatalog &catalog, const Scenario &scenario,
                       int train_index, const QString &traj, double coord,
                       const QVector<TripVia> &vias = QVector<TripVia>());

//------------------------------------------------------------------------------
// Как поезд ведёт себя на станции пути
//------------------------------------------------------------------------------
enum class StopMode
{
    /// Станции нет в расписании
    Skip,
    /// Проезд без остановки (время проезда)
    Pass,
    /// Остановка
    Stop
};

struct StopSpec
{
    /// Индекс в TripPlan::stations
    int station = 0;

    StopMode mode = StopMode::Pass;

    /// Стоянка, с (для остановки; округляется до минуты, не меньше минуты)
    int dwellSec = 60;

    /// Дополнительный запас на перегон до этой станции, мин
    double extraMin = 0.0;

    /// Ручное время прибытия, с от начала суток (-1 - авторасчёт)
    int manualArrival = -1;

    /// Ручное время отправления, с от начала суток (-1 - авторасчёт)
    int manualDeparture = -1;
};

struct TripOptions
{
    /// Время отправления, с от начала суток
    int startTime = 12 * 3600;

    /// Название начальной и конечной точек (пусто - по ближайшей станции)
    QString startName;
    QString endName;

    /// Стоянка на конечной станции (для показа), с
    QVector<StopSpec> stops;

    /// Запас на весь путь, мин (распределяется пропорционально времени хода перегонов)
    double totalExtraMin = 0.0;

    /// Личный запас на перегон до конечной станции, мин
    double endExtraMin = 0.0;

    /// Ручное время прибытия на конечную станцию, с от начала суток (-1 - авторасчёт)
    int endManualArrival = -1;
};

//------------------------------------------------------------------------------
// Строка графика
//------------------------------------------------------------------------------
struct TripRow
{
    QString name;
    double s = 0.0;
    StopMode mode = StopMode::Pass;

    /// Расчётное время хода от отправления без запаса, с (без стоянок)
    double runTime = 0.0;

    /// Время с запасом, с от начала суток, округлено вверх до минуты
    int arrival = 0;
    int departure = 0;

    /// Сколько секунд запаса накоплено к моменту прибытия
    int reserve = 0;

    /// Скорость прохода, км/ч
    double speedKmh = 0.0;

    /// Ручные времена строки (заданы пользователем, идут в экспорт как введены)
    bool manualArrival = false;
    bool manualDeparture = false;

    TripPosition position;
};

struct TripResult
{
    bool ok = false;
    QString error;

    QVector<TripRow> rows;

    /// Время хода по расчёту без запаса и стоянок, с
    double runTime = 0.0;

    QStringList warnings;

    /// Расписание в виде для сценария
    Timetable timetable;
};

/// Рассчитать график. opts.stops - станции, которые попадают в расписание
TripResult computeTrip(const TripPlan &plan, const TripOptions &opts, const Route &route,
                       const Scenario &scenario, int train_index);

/// Запас по умолчанию на весь путь, мин: 10% времени хода, не меньше 2 мин
double defaultReserveMin(double run_time_sec);

/// Время в виде ЧЧ:ММ:СС
QString formatClock(int sec);

} // namespace rse

#endif // RSE_TRIP_H
