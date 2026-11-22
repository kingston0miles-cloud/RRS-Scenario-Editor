#ifndef     RSE_EVENT_DIALOG_H
#define     RSE_EVENT_DIALOG_H

#include    <QDialog>
#include    <QListWidget>

#include    "scenario/events.h"

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QTimeEdit;
class QWidget;

namespace rse
{

//------------------------------------------------------------------------------
// Какое поле заполняется щелчком по карте
//------------------------------------------------------------------------------
enum class EventField
{
    None,
    TriggerTraj,
    TriggerTraj2,
    ActionA,
    ActionB
};

//------------------------------------------------------------------------------
// Событие сценария: триггер и список действий. Действие правится в форме под
// списком; траектории можно указать щелчком по карте
//------------------------------------------------------------------------------
class EventDialog : public QDialog
{
    Q_OBJECT

public:

    enum Result
    {
        PickRequested = 2
    };

    EventDialog(const Route &route, const Scenario &scenario, const ScenarioEvent &event,
                int start_time, int selected_action = -1, QWidget *parent = nullptr);

    ScenarioEvent event() const;

    /// Что выбирали на карте при завершении с кодом PickRequested
    EventField pickField() const
    {
        return pick_field_;
    }

    int selectedAction() const
    {
        return list_->currentRow();
    }

    /// Для проверок
    QPushButton *okButton() const
    {
        return ok_button_;
    }

    QListWidget *actionList() const
    {
        return list_;
    }

    QComboBox *triggerCombo() const
    {
        return trigger_combo_;
    }

    QComboBox *actionTypeCombo() const
    {
        return action_type_;
    }

    QComboBox *fieldA() const
    {
        return edit_a_;
    }

    QComboBox *fieldB() const
    {
        return edit_b_;
    }

    QPushButton *addButton() const
    {
        return add_button_;
    }

    QString problemsText() const;

private slots:

    void onTriggerChanged();
    void onActionSelected();
    void onActionFieldsEdited();
    void onActionTypeChanged();
    void addAction();
    void removeAction();
    void moveAction(int delta);

private:

    const Route &route_;
    const Scenario &scenario_;
    int start_time_ = 0;

    ScenarioEvent event_;
    EventField pick_field_ = EventField::None;
    bool loading_ = false;

    QLineEdit *title_edit_ = nullptr;
    QComboBox *trigger_combo_ = nullptr;

    QWidget *time_row_ = nullptr;
    QLabel *time_label_ = nullptr;
    QTimeEdit *time_edit_ = nullptr;

    QWidget *traj_row_ = nullptr;
    QLabel *traj_label_ = nullptr;
    QComboBox *traj_edit_ = nullptr;
    QPushButton *traj_pick_ = nullptr;

    QWidget *traj2_row_ = nullptr;
    QComboBox *traj2_edit_ = nullptr;
    QPushButton *traj2_pick_ = nullptr;

    QWidget *dir_row_ = nullptr;
    QComboBox *dir_combo_ = nullptr;

    QWidget *train_row_ = nullptr;
    QComboBox *train_combo_ = nullptr;

    QWidget *delay_row_ = nullptr;
    QTimeEdit *delay_edit_ = nullptr;

    QWidget *actions_box_ = nullptr;
    QListWidget *list_ = nullptr;
    QPushButton *add_button_ = nullptr;
    QPushButton *remove_button_ = nullptr;
    QPushButton *up_button_ = nullptr;
    QPushButton *down_button_ = nullptr;

    QWidget *action_form_ = nullptr;
    QComboBox *action_type_ = nullptr;
    QLabel *label_a_ = nullptr;
    QComboBox *edit_a_ = nullptr;
    QPushButton *pick_a_ = nullptr;
    QLabel *label_b_ = nullptr;
    QComboBox *edit_b_ = nullptr;
    QPushButton *pick_b_ = nullptr;
    QLabel *label_dir_ = nullptr;
    QComboBox *action_dir_ = nullptr;

    QPlainTextEdit *problems_ = nullptr;
    QPushButton *ok_button_ = nullptr;

    QStringList trajectoryNames() const;
    QStringList switchNames() const;
    QStringList trainNames() const;

    void fillCombo(QComboBox *box, const QStringList &items, const QString &value);
    void readTrigger();
    void refreshList();
    void refreshProblems();
    void showActionFields();
    void requestPick(EventField field);
};

} // namespace rse

#endif // RSE_EVENT_DIALOG_H
