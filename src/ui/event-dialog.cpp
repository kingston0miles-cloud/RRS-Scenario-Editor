#include    "ui/event-dialog.h"

#include    <QCompleter>
#include    <QComboBox>
#include    <QFormLayout>
#include    <QGroupBox>
#include    <QHBoxLayout>
#include    <QLabel>
#include    <QLineEdit>
#include    <QListWidget>
#include    <QPlainTextEdit>
#include    <QPushButton>
#include    <QSignalBlocker>
#include    <QTime>
#include    <QTimeEdit>
#include    <QVBoxLayout>

namespace rse
{

namespace
{

bool isRouteAction(ActionType t)
{
    return t == ActionType::BuildTrainRoute || t == ActionType::BuildShuntingRoute
        || t == ActionType::SetSwitchesAlongRoute;
}

bool isSwitchAction(ActionType t)
{
    return t == ActionType::SwitchFwd || t == ActionType::SwitchBwd || t == ActionType::OpenSignal
        || t == ActionType::OpenShuntingSignal || t == ActionType::OpenCallSignal
        || t == ActionType::CloseSignal;
}

QWidget *rowOf(QWidget *parent, const std::initializer_list<QWidget *> &widgets)
{
    auto *w = new QWidget(parent);
    auto *l = new QHBoxLayout(w);

    l->setContentsMargins(0, 0, 0, 0);

    for (QWidget *x : widgets)
    {
        l->addWidget(x, x->inherits("QComboBox") ? 1 : 0);
    }

    return w;
}

QComboBox *editableCombo(QWidget *parent)
{
    auto *c = new QComboBox(parent);

    c->setEditable(true);
    c->setInsertPolicy(QComboBox::NoInsert);
    c->completer()->setFilterMode(Qt::MatchContains);
    c->completer()->setCaseSensitivity(Qt::CaseInsensitive);

    return c;
}

} // namespace

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
EventDialog::EventDialog(const Route &route, const Scenario &scenario, const ScenarioEvent &event,
                         int start_time, int selected_action, QWidget *parent)
    : QDialog(parent)
    , route_(route)
    , scenario_(scenario)
    , start_time_(start_time)
    , event_(event)
{
    setWindowTitle(tr("Событие сценария"));
    resize(760, 700);

    auto *layout = new QVBoxLayout(this);

    // Триггер
    auto *trigger_box = new QGroupBox(tr("Когда"), this);
    auto *form = new QFormLayout(trigger_box);

    title_edit_ = new QLineEdit(event_.title, trigger_box);
    title_edit_->setPlaceholderText(tr("Название (для списка и комментария в main.lua)"));
    form->addRow(tr("Название:"), title_edit_);

    trigger_combo_ = new QComboBox(trigger_box);

    for (TriggerType t : { TriggerType::AtTime, TriggerType::AfterStart, TriggerType::TrajBusy,
                           TriggerType::TrajFree, TriggerType::AutoApproach })
    {
        trigger_combo_->addItem(triggerTitle(t), static_cast<int>(t));
    }

    form->addRow(tr("Триггер:"), trigger_combo_);

    time_label_ = new QLabel(trigger_box);
    time_edit_ = new QTimeEdit(trigger_box);
    time_edit_->setDisplayFormat(QStringLiteral("HH:mm:ss"));
    time_row_ = rowOf(trigger_box, { time_edit_ });
    form->addRow(time_label_, time_row_);

    traj_label_ = new QLabel(trigger_box);
    traj_edit_ = editableCombo(trigger_box);
    traj_pick_ = new QPushButton(tr("На карте…"), trigger_box);
    traj_row_ = rowOf(trigger_box, { traj_edit_, traj_pick_ });
    form->addRow(traj_label_, traj_row_);

    traj2_edit_ = editableCombo(trigger_box);
    traj2_pick_ = new QPushButton(tr("На карте…"), trigger_box);
    traj2_row_ = rowOf(trigger_box, { traj2_edit_, traj2_pick_ });
    form->addRow(tr("До траектории:"), traj2_row_);

    dir_combo_ = new QComboBox(trigger_box);
    dir_combo_->addItems({ tr("1 (от начала траектории к концу)"), tr("-1 (от конца к началу)") });
    dir_row_ = rowOf(trigger_box, { dir_combo_ });
    form->addRow(tr("Направление:"), dir_row_);

    train_combo_ = editableCombo(trigger_box);
    train_row_ = rowOf(trigger_box, { train_combo_ });
    form->addRow(tr("Поезд:"), train_row_);

    delay_edit_ = new QTimeEdit(trigger_box);
    delay_edit_->setDisplayFormat(QStringLiteral("HH:mm:ss"));
    delay_row_ = rowOf(trigger_box, { delay_edit_ });
    form->addRow(tr("Выдержка после события:"), delay_row_);

    layout->addWidget(trigger_box);

    // Действия
    actions_box_ = new QGroupBox(tr("Что сделать"), this);
    auto *al = new QVBoxLayout(actions_box_);

    list_ = new QListWidget(actions_box_);
    list_->setMaximumHeight(140);
    al->addWidget(list_);

    auto *bl = new QHBoxLayout;
    add_button_ = new QPushButton(tr("Добавить"), actions_box_);
    remove_button_ = new QPushButton(tr("Удалить"), actions_box_);
    up_button_ = new QPushButton(tr("Выше"), actions_box_);
    down_button_ = new QPushButton(tr("Ниже"), actions_box_);
    bl->addWidget(add_button_);
    bl->addWidget(remove_button_);
    bl->addWidget(up_button_);
    bl->addWidget(down_button_);
    bl->addStretch(1);
    al->addLayout(bl);

    action_form_ = new QWidget(actions_box_);
    auto *af = new QFormLayout(action_form_);
    af->setContentsMargins(0, 4, 0, 0);

    action_type_ = new QComboBox(action_form_);

    for (ActionType t : allActionTypes())
    {
        action_type_->addItem(tr(actionSpec(t).title), static_cast<int>(t));
    }

    af->addRow(tr("Действие:"), action_type_);

    label_a_ = new QLabel(action_form_);
    edit_a_ = editableCombo(action_form_);
    pick_a_ = new QPushButton(tr("На карте…"), action_form_);
    af->addRow(label_a_, rowOf(action_form_, { edit_a_, pick_a_ }));

    label_b_ = new QLabel(action_form_);
    edit_b_ = editableCombo(action_form_);
    pick_b_ = new QPushButton(tr("На карте…"), action_form_);
    af->addRow(label_b_, rowOf(action_form_, { edit_b_, pick_b_ }));

    label_dir_ = new QLabel(tr("Направление:"), action_form_);
    action_dir_ = new QComboBox(action_form_);
    action_dir_->addItems({ tr("1"), tr("-1") });
    af->addRow(label_dir_, action_dir_);

    al->addWidget(action_form_);
    layout->addWidget(actions_box_);

    problems_ = new QPlainTextEdit(this);
    problems_->setReadOnly(true);
    problems_->setMaximumHeight(100);
    problems_->setStyleSheet(QStringLiteral("color: #a02020;"));
    layout->addWidget(problems_);

    auto *buttons = new QHBoxLayout;
    ok_button_ = new QPushButton(tr("OK"), this);
    ok_button_->setDefault(true);
    auto *cancel = new QPushButton(tr("Отмена"), this);
    buttons->addStretch(1);
    buttons->addWidget(ok_button_);
    buttons->addWidget(cancel);
    layout->addLayout(buttons);

    // Начальные значения
    loading_ = true;

    trigger_combo_->setCurrentIndex(trigger_combo_->findData(static_cast<int>(event_.trigger)));
    fillCombo(traj_edit_, trajectoryNames(), event_.traj);
    fillCombo(traj2_edit_, trajectoryNames(), event_.traj2);
    fillCombo(train_combo_, trainNames(), event_.train);
    dir_combo_->setCurrentIndex(event_.dir >= 0 ? 0 : 1);
    delay_edit_->setTime(QTime(0, 0).addSecs(event_.delaySec));
    refreshList();

    loading_ = false;

    onTriggerChanged();

    if (list_->count() > 0)
    {
        list_->setCurrentRow(selected_action >= 0 && selected_action < list_->count() ? selected_action : 0);
    }

    onActionSelected();

    connect(title_edit_, &QLineEdit::textChanged, this, [this] { readTrigger(); });
    connect(trigger_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &EventDialog::onTriggerChanged);
    connect(time_edit_, &QTimeEdit::timeChanged, this, [this] { readTrigger(); });
    connect(delay_edit_, &QTimeEdit::timeChanged, this, [this] { readTrigger(); });
    connect(dir_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { readTrigger(); });

    for (QComboBox *c : { traj_edit_, traj2_edit_, train_combo_ })
    {
        connect(c, &QComboBox::editTextChanged, this, [this] { readTrigger(); });
    }

    connect(traj_pick_, &QPushButton::clicked, this, [this] { requestPick(EventField::TriggerTraj); });
    connect(traj2_pick_, &QPushButton::clicked, this, [this] { requestPick(EventField::TriggerTraj2); });
    connect(pick_a_, &QPushButton::clicked, this, [this] { requestPick(EventField::ActionA); });
    connect(pick_b_, &QPushButton::clicked, this, [this] { requestPick(EventField::ActionB); });

    connect(list_, &QListWidget::currentRowChanged, this, &EventDialog::onActionSelected);
    connect(add_button_, &QPushButton::clicked, this, &EventDialog::addAction);
    connect(remove_button_, &QPushButton::clicked, this, &EventDialog::removeAction);
    connect(up_button_, &QPushButton::clicked, this, [this] { moveAction(-1); });
    connect(down_button_, &QPushButton::clicked, this, [this] { moveAction(1); });
    connect(action_type_, QOverload<int>::of(&QComboBox::activated), this, &EventDialog::onActionTypeChanged);
    connect(edit_a_, &QComboBox::editTextChanged, this, &EventDialog::onActionFieldsEdited);
    connect(edit_b_, &QComboBox::editTextChanged, this, &EventDialog::onActionFieldsEdited);
    connect(action_dir_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &EventDialog::onActionFieldsEdited);

    connect(ok_button_, &QPushButton::clicked, this, &QDialog::accept);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);

    refreshProblems();
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
QStringList EventDialog::trajectoryNames() const
{
    return route_.trajectories().keys();
}

QStringList EventDialog::switchNames() const
{
    QStringList l;

    for (const SwitchData &s : route_.switches())
    {
        l << s.name;
    }

    l.removeDuplicates();
    l.sort();

    return l;
}

QStringList EventDialog::trainNames() const
{
    QStringList l;

    for (const PlacedTrain &t : scenario_.trains())
    {
        l << t.name;
    }

    return l;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void EventDialog::fillCombo(QComboBox *box, const QStringList &items, const QString &value)
{
    const QSignalBlocker b(box);

    box->clear();
    box->addItems(items);
    box->setEditText(value);
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
ScenarioEvent EventDialog::event() const
{
    return event_;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void EventDialog::readTrigger()
{
    if (loading_)
    {
        return;
    }

    event_.title = title_edit_->text();
    event_.trigger = static_cast<TriggerType>(trigger_combo_->currentData().toInt());
    event_.time = QTime(0, 0).secsTo(time_edit_->time());
    event_.traj = traj_edit_->currentText().trimmed();
    event_.traj2 = traj2_edit_->currentText().trimmed();
    event_.dir = dir_combo_->currentIndex() == 0 ? 1 : -1;
    event_.train = train_combo_->currentText().trimmed();
    event_.delaySec = QTime(0, 0).secsTo(delay_edit_->time());

    if (event_.trigger != TriggerType::TrajBusy && event_.trigger != TriggerType::TrajFree)
    {
        event_.train.clear();
        event_.delaySec = 0;
    }

    refreshProblems();
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void EventDialog::onTriggerChanged()
{
    const TriggerType t = static_cast<TriggerType>(trigger_combo_->currentData().toInt());

    const bool timed = (t == TriggerType::AtTime || t == TriggerType::AfterStart);
    const bool pos = (t == TriggerType::TrajBusy || t == TriggerType::TrajFree);
    const bool autoapp = (t == TriggerType::AutoApproach);

    time_row_->setVisible(timed);
    time_label_->setVisible(timed);
    time_label_->setText(t == TriggerType::AtTime ? tr("Время суток:") : tr("Через (после начала игры):"));

    traj_row_->setVisible(pos || autoapp);
    traj_label_->setVisible(pos || autoapp);
    traj_label_->setText(autoapp ? tr("От траектории (занята):") : tr("Траектория:"));

    traj2_row_->setVisible(autoapp);
    dir_row_->setVisible(autoapp);
    train_row_->setVisible(pos);
    delay_row_->setVisible(pos);
    actions_box_->setVisible(!autoapp);

    // Подписи строк формы
    auto *form = qobject_cast<QFormLayout *>(traj2_row_->parentWidget()->layout());

    if (form != nullptr)
    {
        for (QWidget *w : { traj2_row_, dir_row_, train_row_, delay_row_ })
        {
            if (QWidget *label = form->labelForField(w))
            {
                label->setVisible(w->isVisibleTo(this));
            }
        }
    }

    if (!loading_)
    {
        const QTime t0 = time_edit_->time();

        if (t == TriggerType::AtTime && QTime(0, 0).secsTo(t0) < start_time_)
        {
            time_edit_->setTime(QTime(0, 0).addSecs(start_time_));
        }
    }

    readTrigger();
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void EventDialog::refreshList()
{
    const QSignalBlocker b(list_);
    const int row = list_->currentRow();

    list_->clear();

    for (const EventAction &a : std::as_const(event_.actions))
    {
        list_->addItem(actionText(a));
    }

    if (row >= 0 && row < list_->count())
    {
        list_->setCurrentRow(row);
    }
}

//------------------------------------------------------------------------------
// Форма действия показывает выбранное действие
//------------------------------------------------------------------------------
void EventDialog::showActionFields()
{
    const int row = list_->currentRow();
    const bool has = (row >= 0 && row < event_.actions.size());

    action_form_->setEnabled(has);
    remove_button_->setEnabled(has);
    up_button_->setEnabled(has && row > 0);
    down_button_->setEnabled(has && row + 1 < event_.actions.size());

    if (!has)
    {
        return;
    }

    const EventAction &a = event_.actions[row];
    const ActionSpec &sp = actionSpec(a.type);

    loading_ = true;

    action_type_->setCurrentIndex(action_type_->findData(static_cast<int>(a.type)));

    label_a_->setText(tr(sp.labelA) + QLatin1Char(':'));
    label_b_->setText(tr(sp.labelB) + QLatin1Char(':'));

    QStringList items_a;
    QStringList items_b;

    if (isRouteAction(a.type))
    {
        items_a = trajectoryNames();
        items_b = items_a;
    }
    else if (isSwitchAction(a.type))
    {
        items_a = switchNames();
    }
    else
    {
        items_a = trainNames();
    }

    fillCombo(edit_a_, items_a, a.a);
    fillCombo(edit_b_, items_b, a.b);

    const bool two = sp.strings > 1;
    const bool traj = isRouteAction(a.type);

    label_b_->setVisible(two);
    edit_b_->setVisible(two);
    pick_a_->setVisible(traj);
    pick_b_->setVisible(traj);
    label_dir_->setVisible(sp.dir);
    action_dir_->setVisible(sp.dir);
    action_dir_->setCurrentIndex(a.dir >= 0 ? 0 : 1);

    loading_ = false;
}

void EventDialog::onActionSelected()
{
    showActionFields();
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void EventDialog::onActionFieldsEdited()
{
    const int row = list_->currentRow();

    if (loading_ || row < 0 || row >= event_.actions.size())
    {
        return;
    }

    EventAction &a = event_.actions[row];

    a.a = edit_a_->currentText().trimmed();
    a.b = actionSpec(a.type).strings > 1 ? edit_b_->currentText().trimmed() : QString();
    a.dir = action_dir_->currentIndex() == 0 ? 1 : -1;

    list_->item(row)->setText(actionText(a));
    refreshProblems();
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void EventDialog::onActionTypeChanged()
{
    const int row = list_->currentRow();

    if (row < 0 || row >= event_.actions.size())
    {
        return;
    }

    EventAction &a = event_.actions[row];
    const ActionType old = a.type;

    a.type = static_cast<ActionType>(action_type_->currentData().toInt());

    // Поле A другого рода (траектория / стрелка / поезд) - начинаем заново
    const bool same_kind = (isRouteAction(old) == isRouteAction(a.type))
                        && (isSwitchAction(old) == isSwitchAction(a.type));

    if (!same_kind)
    {
        a.a.clear();
        a.b.clear();
    }

    if (actionSpec(a.type).strings < 2)
    {
        a.b.clear();
    }

    list_->item(row)->setText(actionText(a));
    showActionFields();
    refreshProblems();
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void EventDialog::addAction()
{
    EventAction a;

    a.type = ActionType::BuildTrainRoute;

    if (list_->currentRow() >= 0 && list_->currentRow() < event_.actions.size())
    {
        a.type = event_.actions[list_->currentRow()].type;
    }

    event_.actions.push_back(a);
    refreshList();
    list_->setCurrentRow(event_.actions.size() - 1);
    showActionFields();
    refreshProblems();
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void EventDialog::removeAction()
{
    const int row = list_->currentRow();

    if (row < 0 || row >= event_.actions.size())
    {
        return;
    }

    event_.actions.removeAt(row);
    refreshList();
    list_->setCurrentRow(std::min(row, static_cast<int>(event_.actions.size()) - 1));
    showActionFields();
    refreshProblems();
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void EventDialog::moveAction(int delta)
{
    const int row = list_->currentRow();
    const int to = row + delta;

    if (row < 0 || to < 0 || to >= event_.actions.size())
    {
        return;
    }

    event_.actions.swapItemsAt(row, to);
    refreshList();
    list_->setCurrentRow(to);
    showActionFields();
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void EventDialog::requestPick(EventField field)
{
    pick_field_ = field;
    done(PickRequested);
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void EventDialog::refreshProblems()
{
    const EventCheck c = checkEvent(route_, scenario_, event_, start_time_);

    QString text;

    for (const QString &e : c.errors)
    {
        text += QStringLiteral("✗ ") + e + QLatin1Char('\n');
    }

    for (const QString &w : c.warnings)
    {
        text += QStringLiteral("! ") + w + QLatin1Char('\n');
    }

    problems_->setPlainText(text);
    ok_button_->setEnabled(c.errors.isEmpty());
}

QString EventDialog::problemsText() const
{
    return problems_->toPlainText();
}

} // namespace rse
