#ifndef     RSE_CHECKER_H
#define     RSE_CHECKER_H

#include    "scenario/scenario.h"

#include    <QString>
#include    <QStringList>
#include    <QVector>

namespace rse
{

enum class IssueSeverity
{
    Error,
    Warning,
    Info
};

//------------------------------------------------------------------------------
// Одна найденная проблема в расписаниях сценария
//------------------------------------------------------------------------------
struct ScheduleIssue
{
    IssueSeverity severity = IssueSeverity::Warning;

    /// Короткий код вида проблемы: collision, headway, head-on, couple, order,
    /// before-start, no-bot, no-samples, formation …
    QString kind;

    /// Участвующие поезда
    QStringList trains;

    /// Время (с от начала суток) первого проявления, -1 - не привязано ко времени
    int time = -1;
    int timeEnd = -1;

    /// Траектория, где это происходит (может быть пусто)
    QString trajectory;

    QString text;
};

struct CheckOptions
{
    /// Время начала сценария, с
    int startTime = 0;

    /// Шаг сетки времени, с
    int stepSec = 10;

    /// Допустимая дистанция между движущимися поездами, м (ближе - «малый интервал»)
    double headwayMargin = 30.0;
};

/// Проверить все расписания сценария: пересечения, встречное движение, малый интервал,
/// занятые стоянкой пути, ошибки в самих расписаниях. Результат отсортирован:
/// сначала ошибки, затем по времени
QVector<ScheduleIssue> checkSchedules(const Route &route, const Scenario &scenario,
                                      const CheckOptions &options);

/// Одной строкой: «ЧЧ:ММ:СС»
QString clockText(int seconds);

} // namespace rse

#endif // RSE_CHECKER_H
