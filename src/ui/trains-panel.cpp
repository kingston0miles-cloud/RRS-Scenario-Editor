#include    <QMessageBox>
#include    <QStyle>
#include    "ui/trains-panel.h"

#include    "ui/place-train-dialog.h"

#include    <QHBoxLayout>
#include    <QHeaderView>
#include    <QPushButton>
#include    "scenario/trip.h"
#include    <QTreeWidget>
#include    <QVBoxLayout>

namespace rse
{

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
TrainsPanel::TrainsPanel(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);

    tree_ = new QTreeWidget(this);
    tree_->setRootIsDecorated(false);
    tree_->setHeaderLabels({ tr("Имя"), tr("Состав"), tr("Путь"), tr("Координата"),
                             tr("Направление"), tr("Бот"), tr("График") });
    tree_->setSelectionMode(QAbstractItemView::SingleSelection);
    tree_->header()->setStretchLastSection(false);
    tree_->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
    layout->addWidget(tree_, 1);

    auto *row = new QHBoxLayout;

    consist_button_ = new QPushButton(tr("Состав…"), this);
    timetable_button_ = new QPushButton(tr("График…"), this);
    continuation_button_ = new QPushButton(tr("Сцепка…"), this);
    continuation_button_->setProperty("baseToolTip",
                                      tr("Локомотив прибывает к вагонам, сцепляется, и дальше едет "
                                         "новый поезд со своим графиком"));
    continuation_button_->setToolTip(tr("Локомотив прибывает к вагонам, сцепляется, и дальше едет "
                                        "новый поезд со своим графиком"));
    edit_button_ = new QPushButton(tr("Положение…"), this);
    reverse_button_ = new QPushButton(tr("Развернуть на месте"), this);
    remove_button_ = new QPushButton(tr("Удалить"), this);

    row->addWidget(consist_button_);
    row->addWidget(timetable_button_);
    row->addWidget(continuation_button_);
    row->addWidget(edit_button_);
    row->addWidget(reverse_button_);
    row->addStretch(1);
    remove_button_->setStyleSheet(QStringLiteral("QPushButton { color: #b3261e; }"));
    row->addWidget(remove_button_);
    layout->addLayout(row);

    // Неактивные кнопки серые: сразу видно, что действие недоступно
    setStyleSheet(QStringLiteral(
        "QPushButton:disabled { color: #9aa0a6; background: #f0f0f0; }"));

    connect(tree_, &QTreeWidget::currentItemChanged, this, [this]
    {
        updateButtons();
        emit selectionChanged(currentIndex());
    });

    connect(tree_, &QTreeWidget::itemDoubleClicked, this, [this]
    {
        emit showRequested(currentIndex());
    });

    connect(consist_button_, &QPushButton::clicked, this, [this] { emit consistRequested(currentIndex()); });
    connect(timetable_button_, &QPushButton::clicked, this, [this] { emit timetableRequested(currentIndex()); });
    connect(continuation_button_, &QPushButton::clicked, this, [this] { emit continuationRequested(currentIndex()); });
    connect(edit_button_, &QPushButton::clicked, this, [this] { emit editRequested(currentIndex()); });
    connect(reverse_button_, &QPushButton::clicked, this, [this] { emit reverseRequested(currentIndex()); });
    connect(remove_button_, &QPushButton::clicked, this, [this]
    {
        const int i = currentIndex();

        if (scenario_ == nullptr || i < 0 || i >= scenario_->size())
        {
            return;
        }

        // Разрушающее действие - с подтверждением (аудит 2.6)
        const bool has_tt = !scenario_->trains()[i].timetable.stations.isEmpty();

        const QString text = has_tt
            ? tr("У поезда «%1» есть график. Удалить поезд вместе с графиком? "
                 "(действие можно отменить, Ctrl+Z)").arg(scenario_->trains()[i].name)
            : tr("Удалить поезд «%1»? (действие можно отменить, Ctrl+Z)")
                  .arg(scenario_->trains()[i].name);

        if (QMessageBox::question(this, tr("Удаление поезда"), text) != QMessageBox::Yes)
        {
            return;
        }

        emit removeRequested(i);
    });

    updateButtons();
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void TrainsPanel::setScenario(const Scenario *scenario, const Route *route)
{
    scenario_ = scenario;
    route_ = route;
    refresh();
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void TrainsPanel::refresh()
{
    const int keep = currentIndex();

    tree_->blockSignals(true);
    tree_->clear();

    if (scenario_ != nullptr)
    {
        for (const PlacedTrain &t : scenario_->trains())
        {
            const QString dir = (route_ != nullptr)
                ? PlaceTrainDialog::directionText(*route_, t.trajectory, t.dir)
                : QString();

            auto *item = new QTreeWidgetItem(tree_);
            item->setText(0, t.isContinuation() ? QStringLiteral("↪ ") + t.name : t.name);
            item->setText(1, t.title);
            item->setText(2, t.trajectory);
            item->setText(3, QString::number(t.coord, 'f', 1));
            item->setText(4, t.planned ? tr("будущий поезд (только график)") : t.isContinuation()
                                 ? tr("после сцепки: %1 + %2").arg(t.formedFrom, t.formedWagons)
                                 : dir);
            item->setText(5, t.autoMode ? tr("да") : QString());

            // Значки статусов к цветам (аудит 5.1)
            if (t.autoMode)
            {
                item->setIcon(5, style()->standardIcon(QStyle::SP_DialogApplyButton));
            }

            if (t.timetable.valid && !t.timetable.stations.isEmpty())
            {
                item->setText(6, tr("%1 → %2").arg(formatClock(t.timetable.startTime).left(5),
                                                   formatClock(t.timetable.stations.last().arrival).left(5)));
                item->setIcon(6, style()->standardIcon(QStyle::SP_DialogYesButton));
            }
        }

        if (keep >= 0 && keep < tree_->topLevelItemCount())
        {
            tree_->setCurrentItem(tree_->topLevelItem(keep));
        }
    }

    tree_->blockSignals(false);
    updateButtons();
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
int TrainsPanel::currentIndex() const
{
    QTreeWidgetItem *item = tree_->currentItem();

    return item != nullptr ? tree_->indexOfTopLevelItem(item) : -1;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void TrainsPanel::setCurrentIndex(int index)
{
    if (index >= 0 && index < tree_->topLevelItemCount())
    {
        tree_->setCurrentItem(tree_->topLevelItem(index));
    }
    else
    {
        tree_->setCurrentItem(nullptr);
    }
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
int TrainsPanel::rowCount() const
{
    return tree_->topLevelItemCount();
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void TrainsPanel::updateButtons()
{
    const int index = currentIndex();
    const bool has = index >= 0;
    const bool cont = has && scenario_ != nullptr && index < scenario_->size()
                      && scenario_->trains()[index].isContinuation();

    const QString no_selection = tr("Сначала выберите поезд в списке или на карте");
    const QString continuation_hint =
        tr("У поезда-продолжения сцепки положение и состав вычисляются автоматически "
           "из локомотива и вагонов");

    // у продолжения положение и состав вычисляются из локомотива и вагонов
    consist_button_->setEnabled(has && !cont);
    timetable_button_->setEnabled(has);
    continuation_button_->setEnabled(has);
    continuation_button_->setText(cont ? tr("Изменить сцепку…") : tr("Сцепка…"));
    edit_button_->setEnabled(has && !cont);
    reverse_button_->setEnabled(has && !cont);
    remove_button_->setEnabled(has);

    // Неактивная кнопка серая, и подсказка объясняет, почему она неактивна
    const auto set_hint = [no_selection, continuation_hint](QPushButton *b, bool enabled,
                                                            bool cont_reason)
    {
        if (!enabled)
        {
            b->setToolTip(cont_reason ? continuation_hint : no_selection);
        }
        else if (b->property("baseToolTip").isValid())
        {
            b->setToolTip(b->property("baseToolTip").toString());
        }
        else
        {
            b->setToolTip(QString());
        }
    };

    set_hint(consist_button_, has && !cont, cont);
    set_hint(timetable_button_, has, false);
    set_hint(continuation_button_, has, false);
    set_hint(edit_button_, has && !cont, cont);
    set_hint(reverse_button_, has && !cont, cont);
    set_hint(remove_button_, has, false);
}

} // namespace rse
