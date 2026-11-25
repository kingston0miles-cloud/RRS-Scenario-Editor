#include    "ui/export-dialog.h"

#include    "scenario/trip.h"

#include    <QCheckBox>
#include    <QFormLayout>
#include    <QHash>
#include    <QHBoxLayout>
#include    <QHeaderView>
#include    <QLabel>
#include    <QLineEdit>
#include    <QPlainTextEdit>
#include    <QPushButton>
#include    <QTime>
#include    <QTimeEdit>
#include    <QTreeWidget>
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

    triggers_ = new QCheckBox(tr("Триггеры маршрутов по графикам с очерёдностью по категории поезда"), this);
    triggers_->setChecked(true);
    triggers_->setToolTip(tr("Светофоры и стрелки переключаются под расписание; пассажирские "
                             "получают маршруты раньше грузовых, скорые - раньше обычных"));
    form->addRow(QString(), triggers_);

    // Ссылка на просмотр того, что именно уйдёт в main.lua (этап 19, аудит 4.2)
    auto_link_ = new QLabel(this);
    auto_link_->setTextFormat(Qt::RichText);
    auto_link_->setText(tr("<a href=\"#auto\">показать, какие события будут созданы</a>"));
    auto_link_->hide();
    form->addRow(QString(), auto_link_);

    layout->addLayout(form);

    auto_tree_ = new QTreeWidget(this);
    auto_tree_->setHeaderLabels({ tr("Поезд / событие"), tr("Когда") });
    auto_tree_->setColumnWidth(0, 460);
    auto_tree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    auto_tree_->hide();
    layout->addWidget(auto_tree_);

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
    connect(triggers_, &QCheckBox::toggled, this, &ExportDialog::update);
    connect(auto_link_, &QLabel::linkActivated, this, [this]
    {
        auto_details_ = !auto_details_;
        update();
    });

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
    o.autoTriggers = triggers_->isChecked();

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

    // Замечания по важности: что остановит поезд, где возможны заторы, остальное
    {
        const ExportWarningGroups g = groupExportWarnings(plan.warnings);

        auto group = [&text](const QString &title, const QStringList &items, const QString &mark)
        {
            if (items.isEmpty())
            {
                return;
            }

            text += title + QStringLiteral(" (%1):\n").arg(items.size());

            for (const QString &w : items)
            {
                text += QStringLiteral("  %1 ").arg(mark) + w + QLatin1Char('\n');
            }

            text += QLatin1Char('\n');
        };

        group(tr("Поезд может не поехать"), g.stop, QStringLiteral("⚠"));
        group(tr("Возможные заторы и ожидания"), g.jam, QStringLiteral("△"));
        group(tr("Для сведения"), g.info, QStringLiteral("•"));

        if (plan.errors.isEmpty() && plan.warnings.isEmpty())
        {
            text += tr("✓ Всё на месте: замечаний нет.\n\n");
        }
    }

    if (!plan.scenarioDir.isEmpty())
    {
        int timetables = 0;

        for (const QString &f : plan.files)
        {
            timetables += f.startsWith(QLatin1String("timetable/")) ? 1 : 0;
        }

        text += tr("Каталог: %1\nНачало игры: %2\n\nБудет записано: поездов %3, графиков %4, "
                   "событий по графикам %5, событий пользователя %6.\n\nФайлы:\n")
                .arg(plan.scenarioDir, formatClock(plan.startTime).left(5))
                .arg(plan.trains.size()).arg(timetables).arg(plan.autoEvents.size())
                .arg(info_.events.size());

        for (const QString &f : plan.files)
        {
            text += QStringLiteral("  • ") + f + QLatin1Char('\n');
        }
    }

    triggers_->setText(triggers_->isChecked() && !plan.autoEvents.isEmpty()
        ? tr("Триггеры маршрутов по графикам (будет создано событий: %1)").arg(plan.autoEvents.size())
        : tr("Триггеры маршрутов по графикам с очерёдностью по категории поезда"));

    // Просмотр того, что именно будет создано по графикам
    const bool has_auto = triggers_->isChecked() && !plan.autoEvents.isEmpty();
    auto_link_->setVisible(has_auto);
    auto_tree_->setVisible(has_auto && auto_details_);

    if (has_auto && auto_details_)
    {
        fillAutoTree(plan.autoEvents);
    }

    preview_->setPlainText(text);
    export_button_->setEnabled(plan.ok());
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void ExportDialog::fillAutoTree(const QVector<ScenarioEvent> &events)
{
    auto_tree_->clear();

    QHash<QString, QTreeWidgetItem *> groups;

    for (const ScenarioEvent &e : events)
    {
        QTreeWidgetItem *&group = groups[e.owner];

        if (group == nullptr)
        {
            group = new QTreeWidgetItem(auto_tree_);
            group->setText(0, e.owner.isEmpty() ? tr("Прочие") : tr("Поезд «%1»").arg(e.owner));
            group->setExpanded(true);
        }

        auto *item = new QTreeWidgetItem(group);
        item->setText(0, e.title);
        item->setText(1, triggerText(e));
    }
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
QString ExportDialog::previewText() const
{
    return preview_->toPlainText();
}

} // namespace rse
