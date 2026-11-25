#include    "ui/check-dialog.h"

#include    <QApplication>
#include    <QCheckBox>
#include    <QClipboard>
#include    <QHBoxLayout>
#include    <QHeaderView>
#include    <QLabel>
#include    <QPushButton>
#include    <QTableWidget>
#include    <QVBoxLayout>

namespace rse
{

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
CheckDialog::CheckDialog(Provider provider, QWidget *parent)
    : QDialog(parent)
    , provider_(std::move(provider))
{
    setWindowTitle(tr("Проверка графиков сценария"));
    resize(900, 480);

    auto *layout = new QVBoxLayout(this);

    summary_ = new QLabel(this);
    summary_->setWordWrap(true);
    layout->addWidget(summary_);

    table_ = new QTableWidget(0, 4, this);
    table_->setHorizontalHeaderLabels({ tr("Вид"), tr("Время"), tr("Поезда"), tr("Что не так") });
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setWordWrap(true);
    table_->verticalHeader()->hide();
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    layout->addWidget(table_, 1);

    auto *row = new QHBoxLayout;

    only_errors_ = new QCheckBox(tr("Только ошибки и предупреждения"), this);
    only_errors_->setChecked(true);

    auto *again = new QPushButton(tr("Проверить снова"), this);
    auto *copy = new QPushButton(tr("Копировать отчёт"), this);
    auto *close = new QPushButton(tr("Закрыть"), this);

    park_button_ = new QPushButton(tr("Добавить стоянку для пропуска…"), this);
    park_button_->setToolTip(tr("Открыть график выбранного поезда: добавьте стоянку, чтобы "
                                "второй поезд прошёл раньше"));
    park_button_->setEnabled(false);

    row->addWidget(park_button_);
    row->addWidget(only_errors_);
    row->addStretch(1);
    row->addWidget(again);
    row->addWidget(copy);
    row->addWidget(close);
    layout->addLayout(row);

    connect(again, &QPushButton::clicked, this, &CheckDialog::recheck);
    connect(close, &QPushButton::clicked, this, &QDialog::close);
    connect(copy, &QPushButton::clicked, this, [this]
    {
        QApplication::clipboard()->setText(reportText());
    });
    connect(only_errors_, &QCheckBox::toggled, this, &CheckDialog::fill);

    connect(table_, &QTableWidget::cellDoubleClicked, this, [this](int r)
    {
        const int idx = table_->item(r, 0)->data(Qt::UserRole).toInt();

        if (idx >= 0 && idx < issues_.size())
        {
            emit issueActivated(issues_[idx].trains, issues_[idx].trajectory);
        }
    });

    // Кнопка стоянки активна, когда выбрана проблема с участием поездов,
    // которые можно пропустить (пересечение, затор, встречное)
    connect(table_, &QTableWidget::itemSelectionChanged, this, [this]
    {
        const int idx = selectedIssue();
        park_button_->setEnabled(idx >= 0 && !issues_[idx].trains.isEmpty());
    });

    connect(park_button_, &QPushButton::clicked, this, [this]
    {
        const int idx = selectedIssue();

        if (idx >= 0 && !issues_[idx].trains.isEmpty())
        {
            emit parkRequested(issues_[idx].trains.first());
        }
    });

    recheck();
}

//------------------------------------------------------------------------------
// Индекс выбранной проблемы (-1 - ничего не выбрано)
//------------------------------------------------------------------------------
int CheckDialog::selectedIssue() const
{
    const int r = table_->currentRow();

    return (r >= 0 && table_->item(r, 0) != nullptr)
           ? table_->item(r, 0)->data(Qt::UserRole).toInt() : -1;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void CheckDialog::recheck()
{
    issues_ = provider_ ? provider_() : QVector<ScheduleIssue>();
    fill();
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
int CheckDialog::shownCount() const
{
    return table_->rowCount();
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void CheckDialog::fill()
{
    table_->setRowCount(0);

    int errors = 0;
    int warns = 0;
    int infos = 0;

    for (int i = 0; i < issues_.size(); ++i)
    {
        const ScheduleIssue &is = issues_[i];

        switch (is.severity)
        {
        case IssueSeverity::Error: ++errors; break;
        case IssueSeverity::Warning: ++warns; break;
        case IssueSeverity::Info: ++infos; break;
        }

        if (only_errors_->isChecked() && is.severity == IssueSeverity::Info)
        {
            continue;
        }

        const int r = table_->rowCount();
        table_->insertRow(r);

        QString kind;
        QColor color;

        switch (is.severity)
        {
        case IssueSeverity::Error:
            kind = tr("✖ Ошибка");
            color = QColor(0xb0, 0x00, 0x20);
            break;
        case IssueSeverity::Warning:
            kind = tr("⚠ Риск");
            color = QColor(0xa3, 0x5a, 0x00);
            break;
        case IssueSeverity::Info:
            kind = tr("ℹ Заметка");
            color = QColor(0x55, 0x55, 0x55);
            break;
        }

        QString when;

        if (is.time >= 0)
        {
            when = clockText(is.time);

            if (is.timeEnd >= 0 && is.timeEnd != is.time)
            {
                when += QStringLiteral(" – ") + clockText(is.timeEnd);
            }
        }

        auto *k = new QTableWidgetItem(kind);
        k->setForeground(color);
        k->setData(Qt::UserRole, i);

        table_->setItem(r, 0, k);
        table_->setItem(r, 1, new QTableWidgetItem(when));
        table_->setItem(r, 2, new QTableWidgetItem(is.trains.join(QStringLiteral(", "))));
        table_->setItem(r, 3, new QTableWidgetItem(is.text));
    }

    table_->resizeRowsToContents();

    if (issues_.isEmpty())
    {
        summary_->setText(tr("Проблем не найдено: расписания не пересекаются по расчёту."));
    }
    else
    {
        summary_->setText(tr("Ошибок: %1, рисков: %2, заметок: %3. Двойной щелчок выбирает поезд. "
                             "Проверка идёт по расчётным точкам движения головы поезда и длине "
                             "состава; это оценка - в игре поезда могут идти чуть иначе.")
                              .arg(errors).arg(warns).arg(infos));
    }
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
QString CheckDialog::summaryText() const
{
    return summary_->text();
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
QString CheckDialog::reportText() const
{
    QString s;

    for (const ScheduleIssue &is : issues_)
    {
        const QString sev = (is.severity == IssueSeverity::Error) ? tr("ОШИБКА")
                          : (is.severity == IssueSeverity::Warning) ? tr("РИСК") : tr("ЗАМЕТКА");

        s += QStringLiteral("[%1] %2%3\n").arg(sev,
                 is.time >= 0 ? clockText(is.time) + QLatin1Char(' ') : QString(), is.text);
    }

    return s;
}

} // namespace rse
