#include    "ui/export-dialog.h"

#include    "scenario/trip.h"

#include    <QCheckBox>
#include    <QFormLayout>
#include    <QHBoxLayout>
#include    <QLabel>
#include    <QLineEdit>
#include    <QPlainTextEdit>
#include    <QPushButton>
#include    <QTime>
#include    <QTimeEdit>
#include    <QVBoxLayout>

namespace rse
{

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
ExportDialog::ExportDialog(const Route &route, const VehicleCatalog &catalog,
                           const Scenario &scenario, const ProjectInfo &info, QWidget *parent)
    : QDialog(parent)
    , route_(route)
    , catalog_(catalog)
    , scenario_(scenario)
    , info_(info)
{
    setWindowTitle(tr("Экспорт сценария"));
    resize(720, 640);

    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout;

    name_edit_ = new QLineEdit(info.scenarioName, this);
    name_edit_->setPlaceholderText(tr("латиница, цифры, _ и -"));
    form->addRow(tr("Имя сценария (каталог):"), name_edit_);

    auto *start_row = new QHBoxLayout;
    const bool is_auto = info.startTime < 0;
    const int start = is_auto ? suggestStartTime(scenario) : info.startTime;

    auto_start_ = new QCheckBox(tr("по графикам"), this);
    auto_start_->setChecked(is_auto);
    auto_start_->setToolTip(tr("За 3 минуты до первого отправления по графику; без графиков 12:00"));
    start_edit_ = new QTimeEdit(QTime(0, 0).addSecs(start), this);
    start_edit_->setDisplayFormat(QStringLiteral("HH:mm"));
    start_edit_->setEnabled(!is_auto);
    start_row->addWidget(start_edit_);
    start_row->addWidget(auto_start_);
    start_row->addStretch(1);
    form->addRow(tr("Начало игры:"), start_row);

    description_ = new QPlainTextEdit(info.description, this);
    description_->setMaximumHeight(110);
    description_->setPlaceholderText(tr("Текст для README.md сценария"));
    form->addRow(tr("Описание:"), description_);

    overwrite_ = new QCheckBox(tr("Заменить существующий сценарий с таким именем"), this);
    form->addRow(QString(), overwrite_);

    layout->addLayout(form);

    preview_ = new QPlainTextEdit(this);
    preview_->setReadOnly(true);
    layout->addWidget(preview_, 1);

    auto *buttons = new QHBoxLayout;
    export_button_ = new QPushButton(tr("Экспортировать"), this);
    export_button_->setDefault(true);
    auto *cancel = new QPushButton(tr("Отмена"), this);
    buttons->addStretch(1);
    buttons->addWidget(export_button_);
    buttons->addWidget(cancel);
    layout->addLayout(buttons);

    connect(export_button_, &QPushButton::clicked, this, &QDialog::accept);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(name_edit_, &QLineEdit::textChanged, this, &ExportDialog::update);
    connect(start_edit_, &QTimeEdit::timeChanged, this, &ExportDialog::update);
    connect(auto_start_, &QCheckBox::toggled, this, [this](bool on)
    {
        start_edit_->setEnabled(!on);
        update();
    });
    connect(overwrite_, &QCheckBox::toggled, this, &ExportDialog::update);

    update();
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
ExportOptions ExportDialog::options() const
{
    ExportOptions o;

    o.info = info_;
    o.info.scenarioName = name_edit_->text().trimmed();
    o.info.description = description_->toPlainText();
    o.info.startTime = auto_start_->isChecked() ? -1 : QTime(0, 0).secsTo(start_edit_->time());
    o.overwrite = overwrite_->isChecked();

    return o;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void ExportDialog::update()
{
    const ExportOptions o = options();
    const ExportPlan plan = planExport(route_, catalog_, scenario_, o);

    QString text;

    if (!plan.errors.isEmpty())
    {
        text += tr("Экспорт невозможен:\n");

        for (const QString &e : plan.errors)
        {
            text += QStringLiteral("  ✗ ") + e + QLatin1Char('\n');
        }

        text += QLatin1Char('\n');
    }

    if (!plan.warnings.isEmpty())
    {
        text += tr("Замечания:\n");

        for (const QString &w : plan.warnings)
        {
            text += QStringLiteral("  ! ") + w + QLatin1Char('\n');
        }

        text += QLatin1Char('\n');
    }

    if (!plan.scenarioDir.isEmpty())
    {
        text += tr("Каталог: %1\nНачало игры: %2\n\nБудет записано:\n")
                .arg(plan.scenarioDir, formatClock(plan.startTime).left(5));

        for (const QString &f : plan.files)
        {
            text += QStringLiteral("  • ") + f + QLatin1Char('\n');
        }
    }

    preview_->setPlainText(text);
    export_button_->setEnabled(plan.ok());
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
QString ExportDialog::previewText() const
{
    return preview_->toPlainText();
}

} // namespace rse
