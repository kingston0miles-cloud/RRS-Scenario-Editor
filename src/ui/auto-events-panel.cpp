#include    "ui/auto-events-panel.h"

#include    "scenario/autotriggers.h"

#include    <QCheckBox>
#include    <QHeaderView>
#include    <QLabel>
#include    <QTreeWidget>
#include    <QVBoxLayout>

namespace rse
{

namespace
{
constexpr int ROLE_EVENT = Qt::UserRole + 1;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
AutoEventsPanel::AutoEventsPanel(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);

    hint_ = new QLabel(this);
    hint_->setWordWrap(true);
    layout->addWidget(hint_);

    tree_ = new QTreeWidget(this);
    tree_->setHeaderLabels({ tr("Событие"), tr("Когда"), tr("Маршрут") });
    tree_->header()->setSectionResizeMode(QHeaderView::Interactive);
    tree_->setColumnWidth(0, 380);
    tree_->setColumnWidth(1, 240);
    layout->addWidget(tree_, 1);

    connect(tree_, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *item)
    {
        const int idx = (item != nullptr) ? item->data(0, ROLE_EVENT).toInt() - 1 : -1;

        if (idx < 0 || idx >= events_.size() || events_[idx].actions.isEmpty())
        {
            emit selectionCleared();
            return;
        }

        const EventAction &a = events_[idx].actions.first();
        emit routeSelected(a.a, a.b, a.dir);
    });

    setEvents({});
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void AutoEventsPanel::setEvents(const QVector<ScenarioEvent> &events,
                                const QSet<QString> &disabled)
{
    events_ = events;

    tree_->blockSignals(true);
    tree_->clear();

    // Поезд -> группы станции -> события
    QHash<QString, QTreeWidgetItem *> trains;
    QHash<QTreeWidgetItem *, QHash<QString, QTreeWidgetItem *>> stations;
    QHash<QString, int> counts;

    auto train_group = [this, &trains](const QString &owner) -> QTreeWidgetItem *
    {
        QTreeWidgetItem *&group = trains[owner];

        if (group == nullptr)
        {
            group = new QTreeWidgetItem(tree_);
            group->setText(0, owner.isEmpty() ? tr("Прочие") : tr("Поезд «%1»").arg(owner));
            group->setExpanded(false);
            QFont f = group->font(0);
            f.setBold(true);
            group->setFont(0, f);
        }

        return group;
    };

    auto station_group = [&stations, &train_group](QTreeWidgetItem *train,
                                                   const QString &station) -> QTreeWidgetItem *
    {
        if (station.isEmpty())
        {
            return train;
        }

        QTreeWidgetItem *&sub = stations[train][station];

        if (sub == nullptr)
        {
            sub = new QTreeWidgetItem(train);
            sub->setText(0, tr("Станция «%1»").arg(station));
            sub->setExpanded(false);
        }

        return sub;
    };

    for (int i = 0; i < events_.size(); ++i)
    {
        const ScenarioEvent &e = events_[i];
        QTreeWidgetItem *group = station_group(train_group(e.owner), autoEventStation(e.title));

        auto *item = new QTreeWidgetItem(group);
        item->setText(0, e.title);
        item->setText(1, triggerText(e));
        item->setText(2, e.actions.isEmpty() ? QString()
                                             : QStringLiteral("%1 → %2").arg(e.actions.first().a, e.actions.first().b));
        item->setData(0, ROLE_EVENT, i + 1);

        ++counts[e.owner];
    }

    for (auto it = trains.begin(); it != trains.end(); ++it)
    {
        QTreeWidgetItem *group = it.value();

        group->setText(1, tr("событий: %1").arg(counts.value(it.key())));

        // Переключатель «не создавать»: события маршрутов по графикам
        // для этого поезда не создаются вовсе
        auto *off = new QCheckBox(tr("не создавать"), tree_);
        off->setChecked(disabled.contains(it.key()));
        off->setToolTip(tr("Не создавать для поезда события маршрутов по графику: "
                           "светофоры и стрелки переключаться не будут. Тот же флажок - "
                           "в положении поезда"));

        connect(off, &QCheckBox::toggled, this, [this, key = it.key()](bool on)
        {
            emit noAutoEventsChanged(key, on);
        });

        tree_->setItemWidget(group, 2, off);
    }

    tree_->blockSignals(false);

    hint_->setText(events_.isEmpty()
                   ? tr("Событий по графикам нет: у поездов нет расписания или события отключены "
                        "для всех поездов.")
                   : tr("Эти %1 событий редактор создаст сам при экспорте по графикам: маршруты, "
                        "светофоры и стрелки. Выберите событие, чтобы увидеть маршрут на карте. "
                        "Флажок «не создавать» отключает их для поезда.").arg(events_.size()));
}

} // namespace rse
