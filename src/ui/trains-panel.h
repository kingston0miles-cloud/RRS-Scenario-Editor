#ifndef     RSE_TRAINS_PANEL_H
#define     RSE_TRAINS_PANEL_H

#include    <QWidget>

#include    "scenario/scenario.h"

class QPushButton;
class QTreeWidget;

namespace rse
{

//------------------------------------------------------------------------------
// Панель «Поезда сценария»: список поставленных составов, удаление,
// разворот на месте и изменение положения
//------------------------------------------------------------------------------
class TrainsPanel : public QWidget
{
    Q_OBJECT

public:

    explicit TrainsPanel(QWidget *parent = nullptr);

    void setScenario(const Scenario *scenario, const Route *route);

    /// Перечитать список после изменения сценария
    void refresh();

    int currentIndex() const;
    void setCurrentIndex(int index);

    int rowCount() const;

signals:

    void removeRequested(int index);
    void reverseRequested(int index);
    void editRequested(int index);
    void consistRequested(int index);
    void timetableRequested(int index);
    void continuationRequested(int index);

    /// Двойной щелчок: показать поезд на карте
    void showRequested(int index);

    /// Выбранный поезд изменился (-1 - никакой)
    void selectionChanged(int index);

private:

    const Scenario *scenario_ = nullptr;
    const Route *route_ = nullptr;

    QTreeWidget *tree_ = nullptr;
    QPushButton *remove_button_ = nullptr;
    QPushButton *reverse_button_ = nullptr;
    QPushButton *edit_button_ = nullptr;
    QPushButton *consist_button_ = nullptr;
    QPushButton *continuation_button_ = nullptr;
    QPushButton *timetable_button_ = nullptr;

    void updateButtons();
};

} // namespace rse

#endif // RSE_TRAINS_PANEL_H
