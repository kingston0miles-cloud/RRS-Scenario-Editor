#include    "ui/events-panel.h"

#include    <QHBoxLayout>
#include    <QHeaderView>
#include    <QMessageBox>
#include    <QPushButton>
#include    <QTreeWidget>
#include    <QVBoxLayout>

namespace rse
{

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
EventsPanel::EventsPanel(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);

    tree_ = new QTreeWidget(this);
    tree_->setRootIsDecorated(false);
    tree_->setHeaderLabels({ tr("№"), tr("Название"), tr("Когда"), tr("Действий") });
    tree_->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
    tree_->header()->setSectionResizeMode(2, QHeaderView::Stretch);
    layout->addWidget(tree_, 1);

    auto *row = new QHBoxLayout;

    add_ = new QPushButton(tr("Добавить…"), this);
    edit_ = new QPushButton(tr("Изменить…"), this);
    remove_ = new QPushButton(tr("Удалить"), this);
    up_ = new QPushButton(tr("Выше"), this);
    down_ = new QPushButton(tr("Ниже"), this);
    locate_ = new QPushButton(tr("На карте…"), this);
    locate_->setToolTip(tr("Перелететь к месту события на карте (путь события "
                           "подсветится)"));

    for (QPushButton *b : { add_, edit_, remove_, up_, down_, locate_ })
    {
        row->addWidget(b);
    }

    row->addStretch(1);
    layout->addLayout(row);

    connect(add_, &QPushButton::clicked, this, &EventsPanel::addRequested);
    connect(edit_, &QPushButton::clicked, this, [this] { emit editRequested(currentIndex()); });
    // Разрушающее действие - с подтверждением (аудит 2.6)
    connect(remove_, &QPushButton::clicked, this, [this]
    {
        if (currentIndex() < 0)
        {
            return;
        }

        if (QMessageBox::question(this, tr("Удаление события"),
                                  tr("Удалить выбранное событие? "
                                     "(действие можно отменить, Ctrl+Z)")) != QMessageBox::Yes)
        {
            return;
        }

        emit removeRequested(currentIndex());
    });
    connect(up_, &QPushButton::clicked, this, [this] { emit moveRequested(currentIndex(), -1); });
    connect(down_, &QPushButton::clicked, this, [this] { emit moveRequested(currentIndex(), 1); });
    connect(locate_, &QPushButton::clicked, this, [this] { emit locateRequested(currentIndex()); });
    connect(tree_, &QTreeWidget::itemDoubleClicked, this, [this] { emit editRequested(currentIndex()); });
    connect(tree_, &QTreeWidget::currentItemChanged, this, [this] { updateButtons(); });

    // Неактивные кнопки серые, как в панели поездов
    setStyleSheet(QStringLiteral(
        "QPushButton:disabled { color: #9aa0a6; background: #f0f0f0; }"));

    updateButtons();
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void EventsPanel::setEvents(const QVector<ScenarioEvent> *events)
{
    events_ = events;
    refresh();
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void EventsPanel::refresh()
{
    const int keep = currentIndex();

    tree_->blockSignals(true);
    tree_->clear();

    if (events_ != nullptr)
    {
        for (int i = 0; i < events_->size(); ++i)
        {
            const ScenarioEvent &e = (*events_)[i];
            auto *item = new QTreeWidgetItem(tree_);

            item->setText(0, QString::number(i + 1));
            item->setText(1, e.title);
            item->setText(2, triggerText(e));
            item->setText(3, e.trigger == TriggerType::AutoApproach ? QStringLiteral("—")
                                                                    : QString::number(e.actions.size()));
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
int EventsPanel::currentIndex() const
{
    QTreeWidgetItem *item = tree_->currentItem();

    return item != nullptr ? tree_->indexOfTopLevelItem(item) : -1;
}

void EventsPanel::setCurrentIndex(int index)
{
    tree_->setCurrentItem(index >= 0 && index < tree_->topLevelItemCount()
                          ? tree_->topLevelItem(index) : nullptr);
    updateButtons();
}

int EventsPanel::rowCount() const
{
    return tree_->topLevelItemCount();
}

void EventsPanel::updateButtons()
{
    const int i = currentIndex();
    const bool has = i >= 0;

    edit_->setEnabled(has);
    remove_->setEnabled(has);
    up_->setEnabled(has && i > 0);
    down_->setEnabled(has && i + 1 < rowCount());
    locate_->setEnabled(has);
}

} // namespace rse
