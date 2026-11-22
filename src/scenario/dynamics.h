#ifndef     RSE_DYNAMICS_H
#define     RSE_DYNAMICS_H

#include    "catalog/vehicle-catalog.h"
#include    "scenario/scenario.h"

#include    <QString>
#include    <QStringList>
#include    <QVector>

namespace rse
{

//------------------------------------------------------------------------------
// Тяговые возможности состава. Тяговая характеристика локомотива задана в коде
// его модуля, поэтому здесь приближение: постоянная мощность и ограничение
// силы тяги при трогании. Значения можно править вручную
//------------------------------------------------------------------------------
struct TractionProfile
{
    /// Суммарная мощность тяги, кВт
    double powerKw = 3000.0;

    /// Сила тяги при трогании (с учётом всех локомотивов), кН
    double startForceKn = 300.0;

    /// Наибольшая скорость состава, км/ч
    double vMaxKmh = 100.0;

    /// Масса, приходящаяся на ведущие колёса (для ограничения по сцеплению), кг
    double adhesiveMass = 0.0;

    /// Служебное замедление при торможении, м/с^2. Меньше, чем возможно
    /// у состава, поэтому время хода получается с запасом
    double brakeDecel = 0.5;

    /// Откуда взяты значения (для показа пользователю)
    QStringList notes;
};

//------------------------------------------------------------------------------
// Параметры состава для расчёта хода
//------------------------------------------------------------------------------
struct TrainDynamics
{
    /// Масса, кг
    double mass = 0.0;

    /// Длина, м
    double length = 0.0;

    /// Основное сопротивление: F = a0 + a1 v + a2 v^2 (v - м/с, F - Н)
    double a0 = 0.0;
    double a1 = 0.0;
    double a2 = 0.0;

    TractionProfile traction;

    /// Сила сопротивления движению на ровном пути, Н
    double resistance(double v) const;

    /// Сила тяги при скорости v (м/с), Н
    double tractiveForce(double v) const;
};

/// Параметры динамики состава по каталогу (сопротивление по MainResist
/// единиц, тяга - оценка по локомотивам)
TrainDynamics trainDynamics(const PlacedTrain &train, const VehicleCatalog &catalog);

/// Оценка тяги по локомотивам состава
TractionProfile guessTraction(const QVector<ConsistGroup> &groups, const VehicleCatalog &catalog);

//------------------------------------------------------------------------------
// Профиль пути для расчёта: значения на равномерной сетке по длине пути
//------------------------------------------------------------------------------
struct TrackProfile
{
    /// Шаг сетки, м
    double step = 5.0;

    /// Высота пути под головой поезда, м
    QVector<double> height;

    /// Допустимая скорость для состава с учётом его длины (пока хвост
    /// не проехал ограничение, оно действует), м/с
    QVector<double> limit;

    int size() const
    {
        return limit.size();
    }
};

//------------------------------------------------------------------------------
// Результат расчёта хода по профилю
//------------------------------------------------------------------------------
struct RunResult
{
    bool ok = false;
    QString error;

    /// Скорость и время (с от начала) в узлах сетки
    QVector<double> speed;
    QVector<double> time;

    /// Предупреждения (например, нехватка тяги на подъёме)
    QStringList warnings;

    double timeAt(double s, double step) const;
};

/// Рассчитать ход. stops - индексы узлов сетки, где поезд останавливается (первый
/// узел и последний останавливаются всегда). Время стоянок здесь не учитывается
RunResult simulateRun(const TrainDynamics &dyn, const TrackProfile &profile,
                      const QVector<int> &stops);

} // namespace rse

#endif // RSE_DYNAMICS_H
