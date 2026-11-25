#include    "ui/continuation-dialog.h"

#include    "scenario/checker.h"

#include    <QComboBox>
#include    <QDialogButtonBox>
#include    <QFormLayout>
#include    <QLabel>
#include    <QLineEdit>
#include    <QPushButton>
#include    <QSpinBox>
#include    <QVBoxLayout>

namespace rse
{

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
ContinuationDialog::ContinuationDialog(const Route &route, const Scenario &scenario, int loco_index,
                                       const PlacedTrain &cont, int ignore_index, QWidget *parent)
    : QDialog(parent)
    , route_(route)
    , scenario_(scenario)
    , cont_(cont)
    , ignore_index_(ignore_index)
{
    setWindowTitle(tr("Сцепка: локомотив, вагоны и новый график"));
    setMinimumWidth(520);

    const PlacedTrain &loco = scenario.trains()[loco_index];

    auto *layout = new QVBoxLayout(this);

    // Лента процесса сцепки (аудит 3.6): шаги от локомотива до графика нового поезда
    auto *steps = new QLabel(tr("<b>1 · Локомотив</b> → 2 · Вагоны → 3 · Точка сцепки "
                                "→ 4 · График нового поезда"), this);
    steps->setStyleSheet(QStringLiteral("color: #1b5eab;"));
    layout->addWidget(steps);

    auto *intro = new QLabel(
        tr("Локомотив <b>%1</b> по своему графику придёт к стоящим вагонам, сцепится с ними, и "
           "дальше поедет новый поезд со своим графиком. Конечную точку графика локомотива "
           "поставьте вплотную к вагонам (ближе %2 м).")
            .arg(loco.name.toHtmlEscaped()).arg(Scenario::COUPLE_DISTANCE, 0, 'f', 0), this);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    auto *form = new QFormLayout;

    wagons_box_ = new QComboBox(this);

    for (int i = 0; i < scenario.size(); ++i)
    {
        const PlacedTrain &t = scenario.trains()[i];

        if (i == loco_index || t.isContinuation())
        {
            continue;
        }

        wagons_box_->addItem(tr("%1 — %2, %3 м").arg(t.name, t.title).arg(t.length, 0, 'f', 0), t.name);
    }

    const int w = wagons_box_->findData(cont.formedWagons);

    if (w >= 0)
    {
        wagons_box_->setCurrentIndex(w);
    }

    form->addRow(tr("Вагоны:"), wagons_box_);

    name_edit_ = new QLineEdit(cont.name, this);
    form->addRow(tr("Имя нового поезда:"), name_edit_);

    delay_spin_ = new QSpinBox(this);
    delay_spin_->setRange(0, 3600);
    delay_spin_->setSuffix(tr(" с"));
    delay_spin_->setValue(cont.formDelaySec);
    delay_spin_->setToolTip(tr("Через сколько секунд после прибытия локомотива поезд "
                               "переименовывается и включается новый график: сцепка, "
                               "опробование тормозов"));
    form->addRow(tr("Задержка после прибытия:"), delay_spin_);

    layout->addLayout(form);

    info_ = new QLabel(this);
    info_->setWordWrap(true);
    info_->setTextFormat(Qt::RichText);
    layout->addWidget(info_);

    buttons_ = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons_->button(QDialogButtonBox::Ok)->setText(tr("Создать"));
    buttons_->button(QDialogButtonBox::Cancel)->setText(tr("Отмена"));
    layout->addWidget(buttons_);

    connect(buttons_, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons_, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(wagons_box_, &QComboBox::currentIndexChanged, this, &ContinuationDialog::refresh);
    connect(name_edit_, &QLineEdit::textChanged, this, &ContinuationDialog::refresh);
    connect(delay_spin_, &QSpinBox::valueChanged, this, &ContinuationDialog::refresh);

    refresh();
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
QPushButton *ContinuationDialog::okButton() const
{
    return buttons_->button(QDialogButtonBox::Ok);
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
QString ContinuationDialog::infoText() const
{
    return info_->text();
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
PlacedTrain ContinuationDialog::current() const
{
    PlacedTrain c = cont_;

    c.name = name_edit_->text().trimmed();
    c.formedWagons = wagons_box_->currentData().toString();
    c.formDelaySec = delay_spin_->value();

    return c;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
PlacedTrain ContinuationDialog::result() const
{
    const PlacedTrain c = current();
    const FormationPlan plan = planFormation(route_, scenario_, c);

    return plan.ok ? plan.formed : c;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void ContinuationDialog::refresh()
{
    const PlacedTrain c = current();

    QStringList errors;
    QStringList notes;

    if (c.name.isEmpty())
    {
        errors << tr("Задайте имя нового поезда.");
    }
    else
    {
        const int other = scenario_.indexOfName(c.name);

        if (other >= 0 && other != ignore_index_)
        {
            errors << tr("Поезд с именем «%1» уже есть: симулятор не позволит переименовать "
                         "поезд в занятое имя.").arg(c.name.toHtmlEscaped());
        }
    }

    if (c.formedWagons.isEmpty())
    {
        errors << tr("Нет вагонов: сначала поставьте на путь стоящий состав.");
    }
    else
    {
        const FormationPlan plan = planFormation(route_, scenario_, c);

        if (!plan.ok)
        {
            errors << plan.error.toHtmlEscaped();
        }
        else
        {
            for (const QString &w : plan.warnings)
            {
                errors << w.toHtmlEscaped();
            }

            notes << tr("Локомотив прибудет в %1; переименование и новый график - в %2. "
                        "Новый поезд отправляется не раньше этого времени.")
                         .arg(clockText(plan.arrivalTime), clockText(plan.readyTime));
            notes << tr("Новый поезд: %1 м, %2 т. Он разворачивается (reverseTrain) и едет "
                        "в обратную сторону головой вперёд.")
                         .arg(plan.formed.length, 0, 'f', 0).arg(plan.formed.mass / 1000.0, 0, 'f', 0);
        }
    }

    QString html;

    for (const QString &e : std::as_const(errors))
    {
        html += QStringLiteral("<span style='color:#b00020'>✖ %1</span><br>").arg(e);
    }

    for (const QString &n : std::as_const(notes))
    {
        html += QStringLiteral("<span style='color:#444'>ℹ %1</span><br>").arg(n.toHtmlEscaped());
    }

    info_->setText(html);
    okButton()->setEnabled(errors.isEmpty());
}

} // namespace rse
