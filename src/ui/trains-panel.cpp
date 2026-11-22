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
    edit_button_ = new QPushButton(tr("Положение…"), this);
    reverse_button_ = new QPushButton(tr("Развернуть на месте"), this);
    remove_button_ = new QPushButton(tr("Удалить"), this);

    row->addWidget(consist_button_);
    row->addWidget(timetable_button_);
    row->addWidget(edit_button_);
    row->addWidget(reverse_button_);
    row->addWidget(remove_button_);
    layout->addLayout(row);

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
    connect(edit_button_, &QPushButton::clicked, this, [this] { emit editRequested(currentIndex()); });
    connect(reverse_button_, &QPushButton::clicked, this, [this] { emit reverseRequested(currentIndex()); });
    connect(remove_button_, &QPushButton::clicked, this, [this] { emit removeRequested(currentIndex()); });

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
            item->setText(0, t.name);
            item->setText(1, t.title);
            item->setText(2, t.trajectory);
            item->setText(3, QString::number(t.coord, 'f', 1));
            item->setText(4, dir);
            item->setText(5, t.autoMode ? tr("да") : QString());

            if (t.timetable.valid && !t.timetable.stations.isEmpty())
            {
                item->setText(6, tr("%1 → %2").arg(formatClock(t.timetable.startTime).left(5),
                                                   formatClock(t.timetable.stations.last().arrival).left(5)));
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
    const bool has = currentIndex() >= 0;

    consist_button_->setEnabled(has);
    timetable_button_->setEnabled(has);
    edit_button_->setEnabled(has);
    reverse_button_->setEnabled(has);
    remove_button_->setEnabled(has);
}

} // namespace rse
