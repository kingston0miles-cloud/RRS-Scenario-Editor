#ifndef     RSE_EVENTS_PANEL_H
#define     RSE_EVENTS_PANEL_H

#include    <QWidget>

#include    "scenario/events.h"

class QPushButton;
class QTreeWidget;

namespace rse
{

//------------------------------------------------------------------------------
// Список событий сценария (триггеры и действия)
//------------------------------------------------------------------------------
class EventsPanel : public QWidget
{
    Q_OBJECT

public:

    explicit EventsPanel(QWidget *parent = nullptr);

    void setEvents(const QVector<ScenarioEvent> *events);

    void refresh();

    int currentIndex() const;
    void setCurrentIndex(int index);
    int rowCount() const;

signals:

    void addRequested();
    void editRequested(int index);
    void removeRequested(int index);
    void moveRequested(int index, int delta);

private:

    const QVector<ScenarioEvent> *events_ = nullptr;
    QTreeWidget *tree_ = nullptr;
    QPushButton *add_ = nullptr;
    QPushButton *edit_ = nullptr;
    QPushButton *remove_ = nullptr;
    QPushButton *up_ = nullptr;
    QPushButton *down_ = nullptr;

    void updateButtons();
};

} // namespace rse

#endif // RSE_EVENTS_PANEL_H
