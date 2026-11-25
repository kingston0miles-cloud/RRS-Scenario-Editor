#include    "ui/workflow-bar.h"

#include    <QHBoxLayout>
#include    <QPushButton>
#include    <QFont>

namespace rse
{

//------------------------------------------------------------------------------
// Выполнение шага по состоянию
//------------------------------------------------------------------------------
bool WorkflowBar::State::done(int step) const
{
    switch (step)
    {
    case 1: return route;
    case 2: return catalog;
    case 3: return trains;
    case 4: return timetables;
    case 5: return true;            // проверка доступна всегда, не «выполняется»
    case 6: return exported;
    default: return false;
    }
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
WorkflowBar::WorkflowBar(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("workflowBar"));

    const QStringList titles = {
        tr("1 · Маршрут"), tr("2 · Составы"), tr("3 · Расстановка"),
        tr("4 · Графики"), tr("5 · Проверка"), tr("6 · Экспорт")
    };

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(6, 2, 6, 2);
    layout->setSpacing(4);

    for (int i = 0; i < 6; ++i)
    {
        buttons_[i] = new QPushButton(titles[i], this);
        buttons_[i]->setFlat(true);
        buttons_[i]->setCursor(Qt::PointingHandCursor);
        buttons_[i]->setToolTip(tr("Шаг %1 из 6").arg(i + 1));
        connect(buttons_[i], &QPushButton::clicked, this, [this, i] { emit stepActivated(i + 1); });
        layout->addWidget(buttons_[i]);
    }

    layout->addStretch(1);

    for (int i = 0; i < 6; ++i)
    {
        restyle(i);
    }
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void WorkflowBar::setState(const State &state)
{
    state_ = state;

    for (int i = 0; i < 6; ++i)
    {
        restyle(i);
    }
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
int WorkflowBar::currentStep() const
{
    for (int step = 1; step <= 6; ++step)
    {
        if (!state_.done(step))
        {
            return step;
        }
    }

    return 6;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
QStringList WorkflowBar::stepTitles() const
{
    QStringList titles;

    for (const QPushButton *b : buttons_)
    {
        titles << b->text();
    }

    return titles;
}

//------------------------------------------------------------------------------
// Подсветка: выполненный шаг - приглушённо с галочкой, текущий - жирный
// с подчёркиванием, остальные - обычные
//------------------------------------------------------------------------------
void WorkflowBar::restyle(int index)
{
    if (index < 0 || index >= 6)
    {
        return;
    }

    QPushButton *b = buttons_[index];

    if (b == nullptr)
    {
        return;
    }

    const int step = index + 1;
    const bool done = state_.done(step);
    const bool current = (step == currentStep());

    QFont f = b->font();
    f.setBold(current);
    f.setUnderline(false);
    b->setFont(f);

    QString text = b->text();
    const int pos = text.indexOf(QStringLiteral(" ✓"));

    if (pos > 0)
    {
        text.truncate(pos);
    }

    if (done && step != 5)
    {
        text += QStringLiteral(" ✓");
    }

    b->setText(text);
    b->setStyleSheet(done && !current
                     ? QStringLiteral("QPushButton { color: #6f8f6f; }")
                     : (current ? QStringLiteral("QPushButton { color: #1b5eab; }")
                                : QString()));
}

} // namespace rse
