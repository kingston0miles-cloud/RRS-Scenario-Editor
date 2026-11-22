#ifndef     RSE_CONSIST_H
#define     RSE_CONSIST_H

#include    "catalog/vehicle-catalog.h"
#include    "scenario/scenario.h"

#include    <QStringList>
#include    <QVector>

namespace rse
{

/// Зазор между частями разбитого состава, м. Больше дальности автосцепки
/// (10 м), чтобы части не сцепились сразу после старта
constexpr double SPLIT_GAP = 15.0;

/// Состав по единицам (группы раскрыты, count = 1), порядок от головы к хвосту
QVector<ConsistGroup> flattenGroups(const QVector<ConsistGroup> &groups);

/// Склеить подряд идущие одинаковые единицы обратно в группы
QVector<ConsistGroup> compressUnits(const QVector<ConsistGroup> &units);

/// Пересчитать длину, массу и возможность автоведения по каталогу
void updateSummary(PlacedTrain &train, const VehicleCatalog &catalog);

//------------------------------------------------------------------------------
// Результат правки состава: готовые к добавлению поезда и ошибки
//------------------------------------------------------------------------------
struct EditPlan
{
    /// Первый поезд занимает место исходного, остальные ставятся за ним
    QVector<PlacedTrain> pieces;

    /// Что мешает применить правку (пусто - можно)
    QStringList errors;

    /// Предупреждения (например, сцепка в пределах 10 м)
    QStringList warnings;

    bool ok() const
    {
        return errors.isEmpty() && !pieces.isEmpty();
    }
};

//------------------------------------------------------------------------------
// Применить правку к поезду с индексом index (-1 - новый, ещё не добавленный).
// edited - состав после правки (units - его единицы по одной), cuts - номера
// единиц, перед которыми состав разрывается (по возрастанию, > 0).
// Каждая часть ставится позади предыдущей с зазором SPLIT_GAP и проверяется
// на пересечения с остальными поездами и между собой
//------------------------------------------------------------------------------
EditPlan planEdit(const Scenario &scenario, const Route &route, const VehicleCatalog &catalog,
                  int index, const PlacedTrain &edited, const QVector<int> &cuts);

} // namespace rse

#endif // RSE_CONSIST_H
