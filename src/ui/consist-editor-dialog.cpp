#include    "ui/consist-editor-dialog.h"

#include    <QCheckBox>
#include    <QComboBox>
#include    <QDialogButtonBox>
#include    <QDoubleSpinBox>
#include    <QFormLayout>
#include    <QHBoxLayout>
#include    <QHeaderView>
#include    <QLineEdit>
#include    <QLabel>
#include    <QPushButton>
#include    <QSpinBox>
#include    <QTreeWidget>
#include    <QVBoxLayout>

#include    <algorithm>

namespace rse
{

namespace
{

enum Column
{
    ColNumber = 0,
    ColUnit,
    ColLength,
    ColMass,
    ColLoad,
    ColFlags
};

} // namespace

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
ConsistEditorDialog::ConsistEditorDialog(const Scenario &scenario, const Route &route,
                                         const VehicleCatalog &catalog,
                                         const PlacedTrain &train, int index, QWidget *parent)
    : QDialog(parent)
    , scenario_(scenario)
    , route_(route)
    , catalog_(catalog)
    , train_(train)
    , index_(index)
{
    setWindowTitle(tr("Состав поезда «%1»").arg(train.name));
    resize(760, 560);

    for (const ConsistGroup &g : flattenGroups(train.groups))
    {
        Unit u;
        u.group = g;
        units_.push_back(u);
    }

    auto *layout = new QVBoxLayout(this);
    main_layout_ = layout;

    auto *hint = hint_label_ = new QLabel(
        tr("Единицы идут от головы поезда к хвосту. Чтобы разделить состав на несколько "
           "поездов, отметьте «Отделить перед этой единицей»: каждая часть ставится позади "
           "предыдущей с зазором %1 м.").arg(SPLIT_GAP, 0, 'f', 0), this);
    hint->setWordWrap(true);
    layout->addWidget(hint);

    tree_ = new QTreeWidget(this);
    tree_->setRootIsDecorated(false);
    tree_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    tree_->setHeaderLabels({ tr("№"), tr("Единица"), tr("Длина"), tr("Масса"),
                             tr("Загрузка"), tr("Особенности") });
    tree_->header()->setSectionResizeMode(ColUnit, QHeaderView::Stretch);
    layout->addWidget(tree_, 1);

    auto *row = new QHBoxLayout;

    auto *add = new QPushButton(tr("Добавить…"), this);
    auto *remove = new QPushButton(tr("Убрать"), this);
    auto *up = new QPushButton(tr("Выше"), this);
    auto *down = new QPushButton(tr("Ниже"), this);

    row->addWidget(add);
    row->addWidget(remove);
    row->addStretch(1);
    row->addWidget(up);
    row->addWidget(down);
    layout->addLayout(row);

    auto *form = new QFormLayout;

    payload_box_ = new QDoubleSpinBox(this);
    payload_box_->setRange(0.0, 100.0);
    payload_box_->setDecimals(0);
    payload_box_->setSuffix(tr(" %"));
    form->addRow(tr("Загрузка выбранных:"), payload_box_);

    flip_box_ = new QCheckBox(tr("Единица развёрнута (стоит задом)"), this);
    form->addRow(QString(), flip_box_);

    cut_box_ = new QCheckBox(tr("Отделить перед этой единицей"), this);
    form->addRow(QString(), cut_box_);

    auto_box_ = new QCheckBox(tr("Бот: автозапуск и автоведение (для головной части)"), this);
    auto_box_->setChecked(train.autoMode);
    form->addRow(QString(), auto_box_);

    layout->addLayout(form);

    summary_label_ = new QLabel(this);
    layout->addWidget(summary_label_);

    messages_label_ = new QLabel(this);
    messages_label_->setWordWrap(true);
    messages_label_->setTextFormat(Qt::RichText);
    layout->addWidget(messages_label_);

    buttons_ = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons_->button(QDialogButtonBox::Ok)->setText(tr("Применить"));
    buttons_->button(QDialogButtonBox::Cancel)->setText(tr("Отмена"));
    layout->addWidget(buttons_);

    connect(buttons_, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons_, &QDialogButtonBox::rejected, this, &QDialog::reject);

    connect(add, &QPushButton::clicked, this, &ConsistEditorDialog::onAddClicked);
    connect(remove, &QPushButton::clicked, this, [this] { removeUnits(selectedRows()); });

    connect(up, &QPushButton::clicked, this, [this]
    {
        const QVector<int> rows = selectedRows();

        if (rows.size() == 1)
        {
            moveUnit(rows.first(), -1);
        }
    });

    connect(down, &QPushButton::clicked, this, [this]
    {
        const QVector<int> rows = selectedRows();

        if (rows.size() == 1)
        {
            moveUnit(rows.first(), 1);
        }
    });

    connect(tree_, &QTreeWidget::itemSelectionChanged, this, &ConsistEditorDialog::onSelectionChanged);

    connect(payload_box_, &QDoubleSpinBox::valueChanged, this, [this](double v)
    {
        if (!updating_)
        {
            setPayload(selectedRows(), v / 100.0);
        }
    });

    connect(flip_box_, &QCheckBox::clicked, this, [this] { flipUnits(selectedRows()); });

    connect(cut_box_, &QCheckBox::clicked, this, [this](bool on)
    {
        const QVector<int> rows = selectedRows();

        if (rows.size() == 1)
        {
            setCutBefore(rows.first(), on);
        }
    });

    connect(auto_box_, &QCheckBox::clicked, this, [this] { refreshInfo(); });

    rebuild();
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
QPushButton *ConsistEditorDialog::okButton() const
{
    return buttons_->button(QDialogButtonBox::Ok);
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
QString ConsistEditorDialog::messagesText() const
{
    return messages_label_->text();
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
PlacedTrain ConsistEditorDialog::edited() const
{
    PlacedTrain t = train_;

    QVector<ConsistGroup> list;

    for (const Unit &u : units_)
    {
        list.push_back(u.group);
    }

    t.groups = compressUnits(list);
    t.autoMode = auto_box_->isChecked();
    updateSummary(t, catalog_);

    return t;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
QVector<int> ConsistEditorDialog::cuts() const
{
    QVector<int> c;

    for (int i = 1; i < units_.size(); ++i)
    {
        if (units_[i].cutBefore)
        {
            c.push_back(i);
        }
    }

    return c;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
EditPlan ConsistEditorDialog::plan() const
{
    return planEdit(scenario_, route_, catalog_, index_, edited(), cuts());
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
QVector<int> ConsistEditorDialog::selectedRows() const
{
    QVector<int> rows;

    for (QTreeWidgetItem *it : tree_->selectedItems())
    {
        rows.push_back(tree_->indexOfTopLevelItem(it));
    }

    std::sort(rows.begin(), rows.end());

    return rows;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void ConsistEditorDialog::rebuild(const QVector<int> &select)
{
    updating_ = true;

    tree_->clear();

    for (int i = 0; i < units_.size(); ++i)
    {
        const ConsistGroup &g = units_[i].group;
        const VehicleConfigData *v = catalog_.findVehicle(g.configDir, g.config);

        auto *item = new QTreeWidgetItem(tree_);
        item->setText(ColNumber, QString::number(i + 1));
        item->setText(ColUnit, g.config);

        if (v != nullptr)
        {
            const double coeff = std::clamp(g.payloadCoeff, 0.0, 1.0);

            item->setText(ColLength, tr("%1 м").arg(v->length, 0, 'f', 1));
            item->setText(ColMass, tr("%1 т").arg((v->emptyMass + v->payloadMass * coeff) / 1000.0,
                                                  0, 'f', 1));
            item->setText(ColLoad, tr("%1 %").arg(qRound(coeff * 100.0)));
        }
        else
        {
            item->setText(ColLength, tr("нет в каталоге"));
            item->setForeground(ColLength, QColor(170, 40, 40));
        }

        QStringList flags;

        if (v != nullptr && v->isLocomotive)
        {
            flags << tr("локомотив");
        }

        if (!g.forward)
        {
            flags << tr("развёрнута");
        }

        if (units_[i].cutBefore)
        {
            flags << tr("✂ новый поезд");
        }

        item->setText(ColFlags, flags.join(QStringLiteral(", ")));

        if (units_[i].cutBefore)
        {
            for (int c = 0; c < tree_->columnCount(); ++c)
            {
                item->setBackground(c, QColor(255, 240, 200));
            }
        }
    }

    for (int r : select)
    {
        if (r >= 0 && r < tree_->topLevelItemCount())
        {
            tree_->topLevelItem(r)->setSelected(true);
        }
    }

    updating_ = false;

    onSelectionChanged();
    refreshInfo();
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void ConsistEditorDialog::onSelectionChanged()
{
    if (updating_)
    {
        return;
    }

    updating_ = true;

    const QVector<int> rows = selectedRows();

    payload_box_->setEnabled(!rows.isEmpty());
    flip_box_->setEnabled(!rows.isEmpty());
    cut_box_->setEnabled(rows.size() == 1 && rows.first() > 0);

    if (!rows.isEmpty())
    {
        const ConsistGroup &g = units_[rows.first()].group;

        payload_box_->setValue(std::clamp(g.payloadCoeff, 0.0, 1.0) * 100.0);
        flip_box_->setChecked(!g.forward);
        cut_box_->setChecked(rows.size() == 1 && units_[rows.first()].cutBefore);
    }

    updating_ = false;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void ConsistEditorDialog::refreshInfo()
{
    const PlacedTrain t = edited();

    if (composer_)
    {
        int locos = 0;

        for (const Unit &u : units_)
        {
            const VehicleConfigData *v = catalog_.findVehicle(u.group.configDir, u.group.config);
            locos += (v != nullptr && v->isLocomotive);
        }

        summary_label_->setText(tr("Единиц: %1, локомотивов: %2, длина %3 м, масса %4 т.")
                                    .arg(units_.size()).arg(locos)
                                    .arg(t.length, 0, 'f', 1).arg(t.mass / 1000.0, 0, 'f', 1));

        QString html;

        for (const QString &e : catalog_.validateTrain(trainConfig()))
        {
            html += QStringLiteral("<span style='color:#b00020'>✖ %1</span><br>").arg(e.toHtmlEscaped());
        }

        if (!t.autopilotCapable && locos > 0)
        {
            html += QStringLiteral("<span style='color:#a35a00'>⚠ %1</span><br>")
                        .arg(tr("У крайних единиц нет автопилота: флаг «Бот» в сценарии "
                                "для такого состава автоведение не включит."));
        }

        messages_label_->setText(html);
        messages_label_->setVisible(!html.isEmpty());

        okButton()->setEnabled(catalog_.validateTrain(trainConfig()).isEmpty());

        return;
    }

    const EditPlan p = plan();

    auto_box_->setEnabled(t.autopilotCapable);

    if (!t.autopilotCapable)
    {
        auto_box_->setChecked(false);
    }

    int locos = 0;

    for (const Unit &u : units_)
    {
        const VehicleConfigData *v = catalog_.findVehicle(u.group.configDir, u.group.config);
        locos += (v != nullptr && v->isLocomotive);
    }

    summary_label_->setText(tr("Единиц: %1, локомотивов: %2, длина %3 м, масса %4 т. Поездов: %5.")
                                .arg(units_.size()).arg(locos)
                                .arg(t.length, 0, 'f', 1).arg(t.mass / 1000.0, 0, 'f', 1)
                                .arg(std::max<int>(1, p.pieces.size())));

    QString html;

    for (const QString &e : p.errors)
    {
        html += QStringLiteral("<span style='color:#b00020'>✖ %1</span><br>").arg(e.toHtmlEscaped());
    }

    for (const QString &w : p.warnings)
    {
        html += QStringLiteral("<span style='color:#a35a00'>⚠ %1</span><br>").arg(w.toHtmlEscaped());
    }

    messages_label_->setText(html);
    messages_label_->setVisible(!html.isEmpty());

    okButton()->setEnabled(p.ok());
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
bool ConsistEditorDialog::addVehicle(const QString &config_dir, const QString &config,
                                     int count, double payload, int position)
{
    const VehicleConfigData *v = catalog_.findVehicle(config_dir, config);

    if (v == nullptr || count <= 0)
    {
        return false;
    }

    Unit u;
    u.group.module = v->module;
    u.group.configDir = v->configDir;
    u.group.config = v->config;
    u.group.count = 1;
    u.group.forward = true;
    u.group.payloadCoeff = std::clamp(payload, 0.0, 1.0);

    int pos = (position < 0 || position > units_.size()) ? units_.size() : position;
    QVector<int> added;

    for (int i = 0; i < count; ++i)
    {
        units_.insert(pos + i, u);
        added << pos + i;
    }

    rebuild(added);

    return true;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
bool ConsistEditorDialog::addTrain(const QString &name, int count, int position)
{
    const TrainConfigData *t = catalog_.findTrain(name);

    if (t == nullptr || count <= 0)
    {
        return false;
    }

    const QVector<ConsistGroup> flat = flattenGroups(t->groups);

    int pos = (position < 0 || position > units_.size()) ? units_.size() : position;
    QVector<int> added;

    for (int c = 0; c < count; ++c)
    {
        for (const ConsistGroup &g : flat)
        {
            Unit u;
            u.group = g;
            units_.insert(pos, u);
            added << pos++;
        }
    }

    rebuild(added);

    return true;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void ConsistEditorDialog::removeUnits(const QVector<int> &rows)
{
    QVector<int> sorted = rows;
    std::sort(sorted.begin(), sorted.end(), std::greater<int>());

    for (int r : sorted)
    {
        if (r >= 0 && r < units_.size())
        {
            units_.remove(r);
        }
    }

    if (!units_.isEmpty())
    {
        units_[0].cutBefore = false;
    }

    rebuild();
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void ConsistEditorDialog::moveUnit(int row, int delta)
{
    const int to = row + delta;

    if (row < 0 || row >= units_.size() || to < 0 || to >= units_.size())
    {
        return;
    }

    // Отметка «отделить» относится к месту в составе, а не к единице
    const bool a = units_[row].cutBefore;
    const bool b = units_[to].cutBefore;

    std::swap(units_[row], units_[to]);

    units_[row].cutBefore = a;
    units_[to].cutBefore = b;

    rebuild(QVector<int>() << to);
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void ConsistEditorDialog::setPayload(const QVector<int> &rows, double coeff)
{
    for (int r : rows)
    {
        if (r >= 0 && r < units_.size())
        {
            units_[r].group.payloadCoeff = std::clamp(coeff, 0.0, 1.0);
        }
    }

    rebuild(rows);
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void ConsistEditorDialog::flipUnits(const QVector<int> &rows)
{
    for (int r : rows)
    {
        if (r >= 0 && r < units_.size())
        {
            units_[r].group.forward = !units_[r].group.forward;
        }
    }

    rebuild(rows);
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void ConsistEditorDialog::setCutBefore(int row, bool cut)
{
    if (row > 0 && row < units_.size())
    {
        units_[row].cutBefore = cut;
        rebuild(QVector<int>() << row);
    }
}

//------------------------------------------------------------------------------
// Окно выбора добавляемого: единица или готовый состав, число повторов, место
//------------------------------------------------------------------------------
void ConsistEditorDialog::onAddClicked()
{
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Добавить в состав"));

    auto *lay = new QVBoxLayout(&dlg);
    auto *form = new QFormLayout;

    auto *what = new QComboBox(&dlg);

    for (const VehicleConfigData &v : catalog_.vehicles())
    {
        what->addItem(tr("%1 — %2").arg(v.config, v.isLocomotive ? tr("локомотив") : tr("вагон")),
                      QStringList() << QStringLiteral("v") << v.configDir << v.config);
    }

    for (const TrainConfigData &t : catalog_.trains())
    {
        what->addItem(tr("Состав: %1").arg(t.name), QStringList() << QStringLiteral("t") << t.name);
    }

    form->addRow(tr("Что добавить:"), what);

    auto *count = new QSpinBox(&dlg);
    count->setRange(1, 200);
    form->addRow(tr("Сколько раз:"), count);

    auto *payload = new QSpinBox(&dlg);
    payload->setRange(0, 100);
    payload->setValue(100);
    payload->setSuffix(tr(" %"));
    form->addRow(tr("Загрузка (для единицы):"), payload);

    const QVector<int> sel = selectedRows();

    auto *where = new QComboBox(&dlg);
    where->addItem(tr("В конец состава"), -1);
    where->addItem(tr("В начало состава"), 0);

    if (!sel.isEmpty())
    {
        where->addItem(tr("После выбранной единицы"), sel.last() + 1);
        where->setCurrentIndex(2);
    }

    form->addRow(tr("Куда:"), where);

    lay->addLayout(form);

    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    lay->addWidget(bb);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() != QDialog::Accepted || what->count() == 0)
    {
        return;
    }

    const QStringList data = what->currentData().toStringList();
    const int pos = where->currentData().toInt();

    if (data.value(0) == QLatin1String("v"))
    {
        addVehicle(data.value(1), data.value(2), count->value(), payload->value() / 100.0, pos);
    }
    else
    {
        addTrain(data.value(1), count->value(), pos);
    }
}

} // namespace rse

namespace rse
{

namespace
{

const Scenario &emptyScenario()
{
    static const Scenario s;
    return s;
}

const Route &emptyRoute()
{
    static const Route r;
    return r;
}

PlacedTrain fromConfig(const TrainConfigData &cfg)
{
    PlacedTrain t;
    t.name = cfg.name;
    t.title = cfg.title;
    t.groups = cfg.groups;

    return t;
}

} // namespace

//------------------------------------------------------------------------------
// Режим создания состава: те же единицы и операции, без привязки к карте
//------------------------------------------------------------------------------
ConsistEditorDialog::ConsistEditorDialog(const VehicleCatalog &catalog,
                                         const TrainConfigData &initial, QWidget *parent)
    : ConsistEditorDialog(emptyScenario(), emptyRoute(), catalog, fromConfig(initial), -1, parent)
{
    composer_ = true;
    base_ = initial;

    setWindowTitle(initial.groups.isEmpty() ? tr("Новый состав")
                                            : tr("Состав «%1»").arg(initial.name));

    hint_label_->setText(tr("Единицы идут от головы поезда к хвосту. Состав сохраняется в "
                            "cfg/trains игры и затем доступен в списке готовых составов и "
                            "в сценариях."));

    cut_box_->hide();
    auto_box_->hide();

    auto *form = new QFormLayout;

    file_edit_ = new QLineEdit(initial.name, this);
    file_edit_->setPlaceholderText(tr("например, vl60k-1737-t65"));
    form->addRow(tr("Имя файла (cfg/trains/<имя>.xml):"), file_edit_);

    title_edit_ = new QLineEdit(initial.title, this);
    form->addRow(tr("Название:"), title_edit_);

    desc_edit_ = new QLineEdit(initial.description, this);
    form->addRow(tr("Описание:"), desc_edit_);

    charge_box_ = new QDoubleSpinBox(this);
    charge_box_->setRange(0.0, 1.0);
    charge_box_->setDecimals(2);
    charge_box_->setSingleStep(0.01);
    charge_box_->setSuffix(tr(" МПа"));
    charge_box_->setValue(initial.chargingPressure);
    form->addRow(tr("Зарядное давление:"), charge_box_);

    mainres_box_ = new QDoubleSpinBox(this);
    mainres_box_->setRange(0.0, 2.0);
    mainres_box_->setDecimals(2);
    mainres_box_->setSingleStep(0.05);
    mainres_box_->setSuffix(tr(" МПа"));
    mainres_box_->setValue(initial.initMainResPressure);
    form->addRow(tr("Давление в ГР:"), mainres_box_);

    noair_box_ = new QCheckBox(tr("Поезд без воздуха"), this);
    noair_box_->setChecked(initial.noAir);
    form->addRow(QString(), noair_box_);

    main_layout_->insertLayout(1, form);

    okButton()->setText(tr("Сохранить в cfg/trains"));

    connect(file_edit_, &QLineEdit::textChanged, this, [this] { refreshInfo(); });

    refreshInfo();
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
TrainConfigData ConsistEditorDialog::trainConfig() const
{
    TrainConfigData t = base_;

    t.name = file_edit_ != nullptr ? file_edit_->text().trimmed() : base_.name;
    t.title = title_edit_ != nullptr ? title_edit_->text().trimmed() : base_.title;
    t.description = desc_edit_ != nullptr ? desc_edit_->text().trimmed() : base_.description;

    if (charge_box_ != nullptr)
    {
        t.chargingPressure = charge_box_->value();
        t.initMainResPressure = mainres_box_->value();
        t.noAir = noair_box_->isChecked();
    }

    t.groups = edited().groups;

    return t;
}

} // namespace rse
