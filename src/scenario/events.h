#ifndef     RSE_EVENTS_H
#define     RSE_EVENTS_H

#include    "scenario/scenario.h"

#include    <QString>
#include    <QStringList>
#include    <QVector>

namespace rse
{

//------------------------------------------------------------------------------
// Что запускает событие
//------------------------------------------------------------------------------
enum class TriggerType
{
    /// В заданное время суток (setTimeTrigger("ЧЧ:ММ:СС"))
    AtTime,
    /// Через время после начала игры (setTimeTrigger("+ЧЧ:ММ:СС"))
    AfterStart,
    /// Траектория занята поездом (любым или заданным)
    TrajBusy,
    /// Траектория освобождена поездом (любым или заданным)
    TrajFree,
    /// Автоматический пропуск поездов: когда занята траектория traj, строится
    /// маршрут до traj2 (setAutoApproachTrigger). Действий у события нет
    AutoApproach
};

//------------------------------------------------------------------------------
// Что делает событие
//------------------------------------------------------------------------------
enum class ActionType
{
    /// Поездной маршрут от траектории a до траектории b в направлении dir
    BuildTrainRoute,
    /// Маневровый маршрут
    BuildShuntingRoute,
    /// Только перевести стрелки по маршруту
    SetSwitchesAlongRoute,
    /// Стрелка a в плюс / в минус
    SwitchFwd,
    SwitchBwd,
    /// Открыть сигнал на стыке a в направлении dir (поездной, маневровый, пригласительный)
    OpenSignal,
    OpenShuntingSignal,
    OpenCallSignal,
    /// Закрыть сигнал
    CloseSignal,
    /// Переименовать поезд a в b
    RenameTrain,
    /// Развернуть поезд a
    ReverseTrain
};

struct EventAction
{
    ActionType type = ActionType::BuildTrainRoute;
    QString a;
    QString b;
    int dir = 1;
};

struct ScenarioEvent
{
    QString title;

    TriggerType trigger = TriggerType::AtTime;

    /// Время для AtTime (с от начала суток) и AfterStart (с после начала игры)
    int time = 0;

    /// Траектория для TrajBusy/TrajFree и начальная для AutoApproach
    QString traj;

    /// Конечная траектория и направление для AutoApproach
    QString traj2;
    int dir = 1;

    /// Для TrajBusy/TrajFree: только этот поезд (пусто - любой)
    QString train;

    /// Выдержка после срабатывания триггера, с (TrajBusy/TrajFree; 0 - сразу)
    int delaySec = 0;

    QVector<EventAction> actions;

    /// Поезд, ради которого редактор создал событие по графику (пусто - событие пользователя)
    QString owner;
};

/// Что в действии надо задать: число строковых полей и нужно ли направление
struct ActionSpec
{
    const char *title;
    const char *luaName;
    int strings;
    bool dir;
    const char *labelA;
    const char *labelB;
};

const ActionSpec &actionSpec(ActionType type);

/// Все типы действий в порядке показа
const QVector<ActionType> &allActionTypes();

/// Названия типов триггеров
QString triggerTitle(TriggerType type);

/// Краткое описание действия и события для списков
QString actionText(const EventAction &action);
QString triggerText(const ScenarioEvent &event);

struct EventCheck
{
    QStringList errors;
    QStringList warnings;
};

/// Проверить событие: пустые поля - ошибки, неизвестные траектории, стрелки, поезда
/// и недостижимые маршруты - предупреждения. start_time - начало игры, с
EventCheck checkEvent(const Route &route, const Scenario &scenario, const ScenarioEvent &event,
                      int start_time);

/// Lua-текст всех событий (пустая строка, если событий нет)
QString luaEvents(const QVector<ScenarioEvent> &events, int first_index = 0);

QString luaQuote(const QString &s);

/// ЧЧ:ММ:СС (rel - с плюсом)
QString triggerTime(int sec, bool rel);

} // namespace rse

#endif // RSE_EVENTS_H
