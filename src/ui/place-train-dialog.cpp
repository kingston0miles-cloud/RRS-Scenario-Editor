#include    "ui/place-train-dialog.h"

#include    "scenario/autotriggers.h"
#include    <algorithm>
#include    <QCheckBox>
#include    <QComboBox>
#include    <QCompleter>
#include    <QSignalBlocker>
#include    <QDialogButtonBox>
#include    <QDoubleSpinBox>
#include    <QFormLayout>
#include    <QHBoxLayout>
#include    <QLabel>
#include    <QLineEdit>
#include    <QPushButton>
#include    <QVBoxLayout>

namespace rse
{

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
PlaceTrainDialog::PlaceTrainDialog(const Route &route, const Scenario &scenario,
                                   const PlacedTrain &train, int ignore_index,
                                   QWidget *parent)
    : QDialog(parent)
    , route_(route)
    , scenario_(scenario)
    , train_(train)
    , ignore_index_(ignore_index)
{
    setWindowTitle(ignore_index >= 0 ? tr("Изменить положение поезда")
                                     : tr("Поставить поезд на путь"));
    setMinimumWidth(460);

    auto *layout = new QVBoxLayout(this);

    auto *consist = new QLabel(
        tr("<b>%1</b><br>Единиц: %2, длина %3 м, масса %4 т")
            .arg(train_.title.toHtmlEscaped())
            .arg([&] { int n = 0; for (const ConsistGroup &g : train_.groups) n += g.count; return n; }())
            .arg(train_.length, 0, 'f', 1)
            .arg(train_.mass / 1000.0, 0, 'f', 1), this);
    layout->addWidget(consist);

    auto *form = new QFormLayout;

    waypoint_box_ = new QComboBox(this);
    waypoint_box_->addItem(tr("— перенести в путевую точку маршрута —"));

    for (const WaypointData &w : route_.waypoints())
    {
        waypoint_box_->addItem(tr("%1 (голова %2)").arg(w.title, w.dir >= 0 ? tr("к концу") : tr("к началу")));
    }

    form->addRow(tr("Путевая точка:"), waypoint_box_);

    traj_box_ = new QComboBox(this);
    traj_box_->setEditable(true);
    traj_box_->setInsertPolicy(QComboBox::NoInsert);
    traj_box_->addItems(route_.trajectories().keys());
    traj_box_->completer()->setFilterMode(Qt::MatchContains);
    traj_box_->completer()->setCaseSensitivity(Qt::CaseInsensitive);
    traj_box_->setEditText(train_.trajectory);
    form->addRow(tr("Путь:"), traj_box_);

    const TrajectoryData *traj = route_.find(train_.trajectory);

    coord_box_ = new QDoubleSpinBox(this);
    coord_box_->setDecimals(1);
    coord_box_->setSuffix(tr(" м"));
    coord_box_->setRange(0.0, traj != nullptr ? traj->length : 0.0);
    coord_box_->setValue(train_.coord);
    form->addRow(tr("Голова на координате:"), coord_box_);

    auto *dir_row = new QHBoxLayout;
    direction_label_ = new QLabel(this);
    flip_button_ = new QPushButton(tr("Развернуть"), this);
    dir_row->addWidget(direction_label_, 1);
    dir_row->addWidget(flip_button_);
    form->addRow(tr("Голова смотрит:"), dir_row);

    auto_box_ = new QCheckBox(tr("Бот: автозапуск и автоведение"), this);
    auto_box_->setChecked(train_.autoMode && train_.autopilotCapable);
    auto_box_->setEnabled(train_.autopilotCapable);

    if (!train_.autopilotCapable)
    {
        auto_box_->setToolTip(tr("У крайних единиц состава нет модуля автопилота: "
                                 "флаг auto в симуляторе ничего не включит"));
    }

    form->addRow(QString(), auto_box_);

    // Ссылка «Назначить модуль» (аудит 2.4): видна, когда автоведение не включится
    assign_label_ = new QLabel(this);
    assign_label_->setTextFormat(Qt::RichText);
    assign_label_->setWordWrap(true);
    assign_label_->setStyleSheet(QStringLiteral("color: #a35a00;"));
    assign_label_->setVisible(!train_.autopilotCapable);

    const QString assign_dir = (!train_.groups.isEmpty()) ? train_.groups.first().configDir
                                                          : QString();
    const QString unit_hint = (!train_.groups.isEmpty()) ? train_.groups.first().config
                                                         : QString();
    assign_label_->setText(tr("⚠ Автоведение не включится: у крайних единиц состава нет модуля "
                              "автопилота. <a href=\"#assign\">Назначить модуль каталогу «%1»…</a>")
                              .arg(assign_dir));
    connect(assign_label_, &QLabel::linkActivated, this, [this, assign_dir](const QString &)
    {
        emit assignModuleRequested(assign_dir);
    });

    if (!unit_hint.isEmpty())
    {
        assign_label_->setToolTip(tr("Модуль назначается каталогу конфигов единицы «%1» и "
                                     "сохраняется для всех сценариев").arg(unit_hint));
    }

    form->addRow(QString(), assign_label_);

    no_auto_box_ = new QCheckBox(tr("Не создавать события маршрутов по графику этого поезда"), this);
    no_auto_box_->setChecked(train_.noAutoEvents);
    no_auto_box_->setToolTip(tr("Светофоры и стрелки для этого поезда редактор переключать не будет "
                                "(например, если вы настроили события вручную)"));
    form->addRow(QString(), no_auto_box_);

    planned_box_ = new QCheckBox(tr("Поезд появится позже: только график, на карте не ставить"), this);
    planned_box_->setChecked(train_.planned);
    planned_box_->setToolTip(tr("Расписание запишется в timetable/<имя>.xml. Имя должно совпадать "
                                "с именем, которое поезд получит в игре (например, после "
                                "переименования при сцепке)"));
    form->addRow(QString(), planned_box_);

    layout->addLayout(form);

    // Пояснение «как поезд поведёт себя в игре» (аудит 2.3)
    behaviour_label_ = new QLabel(this);
    behaviour_label_->setWordWrap(true);
    behaviour_label_->setStyleSheet(QStringLiteral("color: #444; font-style: italic;"));
    layout->addWidget(behaviour_label_);

    warnings_label_ = new QLabel(this);
    warnings_label_->setWordWrap(true);
    warnings_label_->setTextFormat(Qt::RichText);
    layout->addWidget(warnings_label_);

    buttons_ = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons_->button(QDialogButtonBox::Ok)->setText(tr("Поставить"));
    buttons_->button(QDialogButtonBox::Cancel)->setText(tr("Отмена"));
    layout->addWidget(buttons_);

    connect(buttons_, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons_, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(flip_button_, &QPushButton::clicked, this, &PlaceTrainDialog::onFlip);
    connect(coord_box_, &QDoubleSpinBox::valueChanged, this, &PlaceTrainDialog::refresh);
    connect(traj_box_, &QComboBox::currentTextChanged, this, &PlaceTrainDialog::onTrajectoryEdited);
    connect(waypoint_box_, QOverload<int>::of(&QComboBox::activated), this, &PlaceTrainDialog::onWaypointChosen);
    refresh();
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
QPushButton *PlaceTrainDialog::okButton() const
{
    return buttons_->button(QDialogButtonBox::Ok);
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
QString PlaceTrainDialog::warningsText() const
{
    return warnings_label_->text();
}

//------------------------------------------------------------------------------
// «Туда» - голова смотрит вдоль основной оси маршрута, «обратно» - против
//------------------------------------------------------------------------------
QString PlaceTrainDialog::directionText(const Route &route, const QString &trajectory, int dir)
{
    const int o = route.orientation(trajectory);

    const QString along = (dir >= 0) ? tr("к концу траектории") : tr("к началу траектории");

    if (o == 0)
    {
        if (route.pathDirection(trajectory) == PathDirection::Both)
        {
            return tr("двусторонний путь (движение в обе стороны), %1").arg(along);
        }

        return tr("%1 (правильное направление пути не определено)").arg(along);
    }

    if (dir == o)
    {
        return tr("по ходу пути, «%1», %2").arg(o > 0 ? tr("туда (1)") : tr("обратно (-1)"), along);
    }

    return tr("против хода пути (неправильный путь), %1").arg(along);
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
PlacedTrain PlaceTrainDialog::current() const
{
    PlacedTrain t = train_;

    t.coord = coord_box_->value();
    t.autoMode = auto_box_->isEnabled() && auto_box_->isChecked();
    t.planned = planned_box_->isChecked();
    t.noAutoEvents = no_auto_box_->isChecked();

    return t;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
PlacedTrain PlaceTrainDialog::result() const
{
    return current();
}

//------------------------------------------------------------------------------
// Перенос на другой путь: голова встаёт в начало пути по правильному направлению
//------------------------------------------------------------------------------
void PlaceTrainDialog::onTrajectoryEdited(const QString &text)
{
    const TrajectoryData *traj = route_.find(text);

    if (traj == nullptr || text == train_.trajectory)
    {
        return;
    }

    train_.trajectory = text;

    const int o = route_.orientation(text);
    train_.dir = (o != 0) ? o : 1;

    {
        const QSignalBlocker b(coord_box_);
        coord_box_->setRange(0.0, traj->length);
        coord_box_->setValue(std::min(std::max(train_.coord, 0.0), traj->length));

        // середина пути - чтобы состав гарантированно поместился
        coord_box_->setValue(traj->length / 2.0);
    }

    refresh();
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void PlaceTrainDialog::onWaypointChosen(int index)
{
    if (index <= 0 || index > route_.waypoints().size())
    {
        return;
    }

    const WaypointData &w = route_.waypoints()[index - 1];
    const TrajectoryData *traj = route_.find(w.trajectory);

    if (traj == nullptr)
    {
        return;
    }

    train_.trajectory = w.trajectory;
    train_.dir = (w.dir >= 0) ? 1 : -1;

    {
        const QSignalBlocker b1(coord_box_);
        const QSignalBlocker b2(traj_box_);
        coord_box_->setRange(0.0, traj->length);
        coord_box_->setValue(std::min(std::max(w.coord, 0.0), traj->length));
        traj_box_->setEditText(w.trajectory);
    }

    refresh();
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void PlaceTrainDialog::onFlip()
{
    train_.dir = -train_.dir;
    refresh();
}

//------------------------------------------------------------------------------
// Пояснение «как поезд поведёт себя в игре» (аудит 2.3)
//------------------------------------------------------------------------------
QString PlaceTrainDialog::gameBehaviourText() const
{
    const PlacedTrain t = current();

    if (t.planned)
    {
        return tr("В игре: поезд появится позже и сразу поедет по графику timetable/%1.xml; "
                  "в начале игры на карте его нет.").arg(t.name);
    }

    if (t.autoMode && t.autopilotCapable)
    {
        return tr("В игре: поезд стоит на путях, а с открытием маршрутов автопилот ведёт его "
                  "по графику — отправление и скорости по расписанию, светофоры и стрелки "
                  "переключаются маршрутами графика.");
    }

    return tr("В игре: поезд стоит на путях и ждёт. Автоведение не включится (нет модуля "
              "автопилота у крайних единиц), движением управят события сценария или "
              "ручное управление.");
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void PlaceTrainDialog::refresh()
{
    const PlacedTrain t = current();

    direction_label_->setText(directionText(route_, t.trajectory, t.dir));
    behaviour_label_->setText(gameBehaviourText());

    const PlacementCheck chk = scenario_.check(route_, t, ignore_index_);

    QStringList errors;
    QStringList warns;

    if (t.name.isEmpty())
    {
        errors << tr("Задайте имя поезда.");
    }
    else
    {
        const int other = scenario_.indexOfName(t.name);

        if (other >= 0 && other != ignore_index_)
        {
            errors << tr("Поезд с именем «%1» уже есть.").arg(t.name.toHtmlEscaped());
        }
    }

    if (chk.overlap > 0.0)
    {
        errors << tr("Состав пересекается с поездом: %1 (на %2 м).")
                      .arg(chk.overlapWith.join(QStringLiteral(", ")).toHtmlEscaped())
                      .arg(chk.overlap, 0, 'f', 1);
    }

    if (chk.footprint.deadEnd)
    {
        errors << tr("Состав не умещается на путях: хвост упирается в тупик, "
                     "не хватает %1 м. Сдвиньте голову или разверните состав.")
                      .arg(chk.footprint.overhang, 0, 'f', 1);
    }

    if (route_.orientation(t.trajectory) != 0 && route_.orientation(t.trajectory) != t.dir)
    {
        warns << tr("Голова смотрит против хода пути: на неправильном пути поездные светофоры "
                    "по направлению движения не откроются.");
    }

    if (!chk.nearTrains.isEmpty())
    {
        warns << tr("Ближе %1 м до поезда %2: в игре они сцепятся автоматически.")
                     .arg(Scenario::COUPLE_DISTANCE, 0, 'f', 0)
                     .arg(chk.nearTrains.join(QStringLiteral(", ")).toHtmlEscaped());
    }

    if (chk.footprint.ambiguous)
    {
        warns << tr("Состав стоит через стрелку, положение которой по данным маршрута "
                    "однозначно не определить; расчёт принимает «плюс».");
    }

    QString html;

    for (const QString &e : std::as_const(errors))
    {
        html += QStringLiteral("<span style='color:#b00020'>✖ %1</span><br>").arg(e);
    }

    for (const QString &w : std::as_const(warns))
    {
        html += QStringLiteral("<span style='color:#a35a00'>⚠ %1</span><br>").arg(w);
    }

    warnings_label_->setText(html);
    warnings_label_->setVisible(!html.isEmpty());

    okButton()->setEnabled(errors.isEmpty());
}

} // namespace rse
