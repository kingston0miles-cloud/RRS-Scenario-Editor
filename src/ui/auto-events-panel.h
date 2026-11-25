#ifndef     RSE_AUTO_EVENTS_PANEL_H
#define     RSE_AUTO_EVENTS_PANEL_H

#include    "scenario/events.h"

#include    <QSet>
#include    <QWidget>

class QTreeWidget;
class QLabel;

namespace rse
{

//------------------------------------------------------------------------------
// События, которые редактор создаёт сам по графикам поездов (только для чтения).
// Сгруппированы по поездам и станциям; выбранное событие показывает свой маршрут
// на карте. У каждого поезда - переключатель «не создавать»: события маршрутов
// по графикам для него не создаются вовсе
//------------------------------------------------------------------------------
class AutoEventsPanel : public QWidget
{
    Q_OBJECT

public:

    explicit AutoEventsPanel(QWidget *parent = nullptr);

    /// События по графикам и поезда, для которых они отключены
    /// (флажок «не создавать»)
    void setEvents(const QVector<ScenarioEvent> &events,
                   const QSet<QString> &disabled = QSet<QString>());

    int eventCount() const
    {
        return events_.size();
    }

signals:

    /// Выбрано событие с маршрутом: траектории и направление
    void routeSelected(const QString &from, const QString &to, int dir);

    void selectionCleared();

    /// Переключатель «не создавать» у поезда: события маршрутов для него
    /// не создаются (true) или создаются снова (false)
    void noAutoEventsChanged(const QString &train, bool disabled);

private:

    QVector<ScenarioEvent> events_;
    QTreeWidget *tree_ = nullptr;
    QLabel *hint_ = nullptr;
};

} // namespace rse

#endif // RSE_AUTO_EVENTS_PANEL_H
