#ifndef     RSE_AUTOTRIGGERS_H
#define     RSE_AUTOTRIGGERS_H

#include    "scenario/events.h"
#include    "scenario/scenario.h"

#include    <QHash>
#include    <QString>
#include    <QStringList>
#include    <QVector>

namespace rse
{

//------------------------------------------------------------------------------
// Категории поездов и очерёдность их пропуска. Игра сама приоритетов не знает: кто
// первым запросил маршрут, тот его и получил. Редактор выстраивает очерёдность
// триггерами: маршрут поезда низшей категории строится только после освобождения
// общих путей поездом высшей категории, если их графики пересекаются
//------------------------------------------------------------------------------
enum class TrainClass
{
    /// По номеру поезда
    Auto,
    /// Скорые, скоростные, фирменные
    Express,
    /// Пассажирские дальнего следования
    Passenger,
    /// Пригородные
    Suburban,
    /// Грузовые
    Freight,
    /// Резервом, хозяйственные, одиночные локомотивы
    Service
};

QString trainClassTitle(TrainClass cls);

/// Категория по номеру поезда (по умолчанию - грузовой)
TrainClass guessTrainClass(const QString &name);

/// Категория поезда: заданная или определённая по номеру
TrainClass effectiveClass(const PlacedTrain &train);

/// Станция авто-события по заголовку («Приём: … на станцию «X»», «Отправление: …
/// со станции «X»»); пусто - выделить не удалось
QString autoEventStation(const QString &title);

/// Чем меньше число, тем выше приоритет
int classPriority(TrainClass cls);

struct AutoTriggerOptions
{
    /// Окно, в котором пересечение графиков считается спором за путь, с
    int margin = 300;

    /// Строить триггеры и для ботов (иначе - только для поездов без автопилота и
    /// для тех маршрутов, где нужна очерёдность)
    bool forBots = false;

    /// Дублировать маршруты событиями по времени графика с повторами, если маршрут
    /// не удалось построить сразу (путь занят): светофоры открываются, когда придёт время
    bool byTime = true;

    /// Начало игры, с: события по времени раньше него не создаются
    int startTime = 0;

    /// Через сколько секунд повторять попытку построить маршрут
    QVector<int> retries = { 60, 180 };
};

/// Путь, который занимает маршрут поезда, и время занятия
struct RouteClaim
{
    QString train;
    QString station;
    bool arrival = true;
    QStringList trajectories;
    int t0 = 0;
    int t1 = 0;
};

struct RouteConflict
{
    QString trainA;
    QString trainB;
    QString trajectory;
    int t0 = 0;
    int t1 = 0;
};

struct AutoTriggerPlan
{
    /// Маршруты всех поездов с расписанием
    QVector<RouteClaim> claims;

    QVector<ScenarioEvent> events;

    /// Для станций с маршрутом по триггерам подход/уход в расписании очищается,
    /// чтобы автопилот не занял путь первым: имя поезда -> по станциям
    QHash<QString, QVector<bool>> clearApproach;
    QHash<QString, QVector<bool>> clearRemoval;

    QStringList warnings;
    QStringList notes;
};

/// Маршруты поездов одной категории, которые одновременно занимают одну траекторию:
/// очерёдность триггерами для них не задаётся, кому достанется путь, решит случай
QVector<RouteConflict> routeConflicts(const AutoTriggerPlan &plan, const Scenario &scenario);

/// Триггеры по расписаниям поездов. start_time - начало игры, с
AutoTriggerPlan planAutoTriggers(const Route &route, const Scenario &scenario,
                                 const AutoTriggerOptions &options = AutoTriggerOptions());

} // namespace rse

#endif // RSE_AUTOTRIGGERS_H
