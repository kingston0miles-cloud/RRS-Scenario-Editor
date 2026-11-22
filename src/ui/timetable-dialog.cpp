#include    "ui/timetable-dialog.h"

#include    <QComboBox>
#include    <QDoubleSpinBox>
#include    <QFormLayout>
#include    <QGroupBox>
#include    <QHBoxLayout>
#include    <QHeaderView>
#include    <QLabel>
#include    <QPlainTextEdit>
#include    <QPushButton>
#include    <QSignalBlocker>
#include    <QTableWidget>
#include    <QTimeEdit>
#include    <QVBoxLayout>

#include    <cmath>

namespace rse
{

namespace
{

QString minutesText(double sec)
{
    const int total = static_cast<int>(std::lround(sec));

    return QObject::tr("%1 мин %2 с").arg(total / 60).arg(total % 60);
}

} // namespace

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
TimetableState TimetableState::fromTimetable(const Timetable &tt)
{
    TimetableState st;

    if (!tt.valid)
    {
        return st;
    }

    st.hasEnd = true;
    st.endTrajectory = tt.endTrajectory;
    st.endCoord = tt.endCoord;
    st.startTime = tt.startTime;
    st.totalExtraMin = tt.totalExtraMin;

    for (int k = 0; k < tt.stations.size(); ++k)
    {
        const TimetableStation &s = tt.stations[k];

        if (k == 0)
        {
            st.startName = s.name;
            continue;
        }

        if (k + 1 == tt.stations.size())
        {
            st.endName = s.name;
            st.endExtraMin = s.extraMin;
            continue;
        }

        StationSetting ss;
        ss.included = true;
        ss.stop = s.stops();
        ss.dwellMin = std::max(1, (s.departure - s.arrival) / 60);
        ss.extraMin = s.extraMin;
        st.stations.insert(s.name, ss);
    }

    return st;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
TimetableDialog::TimetableDialog(const Route &route, const VehicleCatalog &catalog,
                                 const Scenario &scenario, int train_index,
                                 const TimetableState &state, QWidget *parent)
    : QDialog(parent)
    , route_(route)
    , catalog_(catalog)
    , scenario_(scenario)
    , train_index_(train_index)
    , state_(state)
{
    const PlacedTrain &train = scenario_.trains()[train_index_];

    setWindowTitle(tr("График поезда «%1»").arg(train.name));
    resize(980, 640);

    auto *layout = new QVBoxLayout(this);

    // Начало и конец
    auto *path_box = new QGroupBox(tr("Путь"), this);
    auto *path_form = new QFormLayout(path_box);

    auto *from_label = new QLabel(tr("%1, путь «%2», %3 м").arg(train.name, train.trajectory)
                                  .arg(train.coord, 0, 'f', 0), path_box);
    path_form->addRow(tr("От:"), from_label);

    auto *end_row = new QHBoxLayout;
    end_label_ = new QLabel(tr("не выбрана"), path_box);
    end_label_->setWordWrap(true);
    waypoint_combo_ = new QComboBox(path_box);
    waypoint_combo_->addItem(tr("— путевая точка —"));

    for (const WaypointData &w : route_.waypoints())
    {
        waypoint_combo_->addItem(w.title);
    }

    auto *pick = new QPushButton(tr("Указать на карте…"), path_box);
    end_row->addWidget(end_label_, 1);
    end_row->addWidget(waypoint_combo_);
    end_row->addWidget(pick);
    path_form->addRow(tr("До:"), end_row);

    start_edit_ = new QTimeEdit(QTime(0, 0).addSecs(state_.startTime), path_box);
    start_edit_->setDisplayFormat(QStringLiteral("HH:mm"));

    total_extra_ = new QDoubleSpinBox(path_box);
    total_extra_->setRange(0.0, 600.0);
    total_extra_->setDecimals(1);
    total_extra_->setSingleStep(1.0);
    total_extra_->setSuffix(tr(" мин"));
    total_extra_->setToolTip(tr("Распределяется по перегонам пропорционально времени хода"));

    if (state_.totalExtraMin >= 0.0)
    {
        total_extra_->setValue(state_.totalExtraMin);
    }

    auto *def_button = new QPushButton(tr("По умолчанию (10%)"), path_box);

    auto *time_row = new QHBoxLayout;
    time_row->addWidget(new QLabel(tr("Отправление:"), path_box));
    time_row->addWidget(start_edit_);
    time_row->addSpacing(16);
    time_row->addWidget(new QLabel(tr("Запас на весь путь:"), path_box));
    time_row->addWidget(total_extra_);
    time_row->addWidget(def_button);
    time_row->addStretch(1);
    path_form->addRow(time_row);

    layout->addWidget(path_box);

    // Тяга
    auto *traction_box = new QGroupBox(tr("Тяга и торможение (приближённо)"), this);
    auto *tr_layout = new QHBoxLayout(traction_box);

    auto make_spin = [traction_box](double min, double max, double step, int dec, const QString &suffix)
    {
        auto *s = new QDoubleSpinBox(traction_box);
        s->setRange(min, max);
        s->setSingleStep(step);
        s->setDecimals(dec);
        s->setSuffix(suffix);
        return s;
    };

    power_ = make_spin(10.0, 50000.0, 100.0, 0, tr(" кВт"));
    force_ = make_spin(1.0, 5000.0, 10.0, 0, tr(" кН"));
    vmax_ = make_spin(5.0, 400.0, 5.0, 0, tr(" км/ч"));
    brake_ = make_spin(0.1, 1.5, 0.05, 2, tr(" м/с²"));

    tr_layout->addWidget(new QLabel(tr("Мощность:"), traction_box));
    tr_layout->addWidget(power_);
    tr_layout->addWidget(new QLabel(tr("Сила тяги при трогании:"), traction_box));
    tr_layout->addWidget(force_);
    tr_layout->addWidget(new QLabel(tr("Макс. скорость:"), traction_box));
    tr_layout->addWidget(vmax_);
    tr_layout->addWidget(new QLabel(tr("Торможение:"), traction_box));
    tr_layout->addWidget(brake_);
    traction_note_ = new QLabel(traction_box);
    traction_note_->setWordWrap(true);
    tr_layout->addWidget(traction_note_, 1);

    layout->addWidget(traction_box);

    // Таблица
    table_ = new QTableWidget(this);
    table_->setColumnCount(ColCount);
    table_->setHorizontalHeaderLabels({ tr("В графике"), tr("Станция"), tr("км"), tr("Режим"),
                                        tr("Стоянка, мин"), tr("Запас до станции, мин"),
                                        tr("Прибытие"), tr("Отправление"), tr("Скорость, км/ч") });
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(ColName, QHeaderView::Stretch);
    table_->verticalHeader()->hide();
    table_->setSelectionMode(QAbstractItemView::NoSelection);
    layout->addWidget(table_, 1);

    summary_ = new QLabel(this);
    summary_->setWordWrap(true);
    layout->addWidget(summary_);

    warnings_ = new QPlainTextEdit(this);
    warnings_->setReadOnly(true);
    warnings_->setMaximumHeight(90);
    warnings_->setStyleSheet(QStringLiteral("color: #a02020;"));
    layout->addWidget(warnings_);

    auto *buttons = new QHBoxLayout;
    auto *remove = new QPushButton(tr("Убрать расписание"), this);
    apply_button_ = new QPushButton(tr("Применить"), this);
    apply_button_->setDefault(true);
    auto *cancel = new QPushButton(tr("Отмена"), this);

    buttons->addWidget(remove);
    buttons->addStretch(1);
    buttons->addWidget(apply_button_);
    buttons->addWidget(cancel);
    layout->addLayout(buttons);

    connect(table_, &QTableWidget::itemChanged, this, [this] { recompute(); });
    connect(pick, &QPushButton::clicked, this, [this] { done(PickRequested); });
    connect(remove, &QPushButton::clicked, this, [this] { done(RemoveRequested); });
    connect(apply_button_, &QPushButton::clicked, this, &QDialog::accept);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(def_button, &QPushButton::clicked, this, &TimetableDialog::onDefaultReserve);
    connect(waypoint_combo_, QOverload<int>::of(&QComboBox::activated),
            this, &TimetableDialog::onWaypointChosen);

    connect(start_edit_, &QTimeEdit::timeChanged, this, &TimetableDialog::recompute);
    connect(total_extra_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &TimetableDialog::recompute);

    for (QDoubleSpinBox *s : { power_, force_, vmax_, brake_ })
    {
        connect(s, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this]
        {
            if (!loading_)
            {
                markEdited();
                recompute();
            }
        });
    }

    rebuildPlan();
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void TimetableDialog::markEdited()
{
    state_.tractionEdited = true;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void TimetableDialog::setEnd(const QString &trajectory, double coord)
{
    state_.hasEnd = true;
    state_.endTrajectory = trajectory;
    state_.endCoord = coord;
    state_.endName.clear();
    rebuildPlan();
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void TimetableDialog::onWaypointChosen(int index)
{
    if (index <= 0 || index > route_.waypoints().size())
    {
        return;
    }

    const WaypointData &w = route_.waypoints()[index - 1];

    setEnd(w.trajectory, w.coord);
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void TimetableDialog::onDefaultReserve()
{
    total_extra_->setValue(defaultReserveMin(result_.runTime));
}

//------------------------------------------------------------------------------
// Пересчитать путь (после смены конечной точки)
//------------------------------------------------------------------------------
void TimetableDialog::rebuildPlan()
{
    loading_ = true;

    plan_ = TripPlan();
    result_ = TripResult();

    if (state_.hasEnd)
    {
        plan_ = buildTripPlan(route_, catalog_, scenario_, train_index_,
                              state_.endTrajectory, state_.endCoord);
        end_label_->setText(tr("путь «%1», %2 м").arg(state_.endTrajectory)
                            .arg(state_.endCoord, 0, 'f', 0));
    }
    else
    {
        plan_.error = tr("Укажите конечную точку: путевую точку из списка или щелчком по карте");
        end_label_->setText(tr("не выбрана"));
    }

    // Тяга: оценка по составу или правки пользователя
    TrainDynamics dyn = trainDynamics(scenario_.trains()[train_index_], catalog_);

    if (plan_.ok)
    {
        dyn = plan_.dynamics;
    }

    if (state_.tractionEdited)
    {
        dyn.traction.powerKw = state_.powerKw;
        dyn.traction.startForceKn = state_.startForceKn;
        dyn.traction.vMaxKmh = state_.vMaxKmh;
        dyn.traction.brakeDecel = state_.brakeDecel;
    }

    power_->setValue(dyn.traction.powerKw);
    force_->setValue(dyn.traction.startForceKn);
    vmax_->setValue(dyn.traction.vMaxKmh);
    brake_->setValue(dyn.traction.brakeDecel);
    traction_note_->setText(dyn.traction.notes.join(QLatin1Char(' ')));

    loading_ = false;

    fillTable();
    recompute();
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void TimetableDialog::fillTable()
{
    QSignalBlocker block(table_);

    table_->clearContents();
    table_->setRowCount(0);
    row_station_.clear();

    if (!plan_.ok)
    {
        return;
    }

    auto add_row = [this](int station, const QString &name, double s, bool fixed)
    {
        const int r = table_->rowCount();
        table_->insertRow(r);
        row_station_.push_back(station);

        auto *use = new QTableWidgetItem;
        use->setFlags(Qt::ItemIsEnabled | (fixed ? Qt::NoItemFlags : Qt::ItemIsUserCheckable));
        use->setCheckState(Qt::Checked);
        table_->setItem(r, ColUse, use);

        auto *nm = new QTableWidgetItem(name);
        nm->setFlags(fixed ? (Qt::ItemIsEnabled | Qt::ItemIsEditable) : Qt::ItemIsEnabled);
        table_->setItem(r, ColName, nm);

        auto *km = new QTableWidgetItem(QString::number(s / 1000.0, 'f', 2));
        km->setFlags(Qt::ItemIsEnabled);
        km->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        table_->setItem(r, ColKm, km);

        for (int c : { ColArrival, ColDeparture, ColSpeed })
        {
            auto *it = new QTableWidgetItem;
            it->setFlags(Qt::ItemIsEnabled);
            it->setTextAlignment(Qt::AlignCenter);
            table_->setItem(r, c, it);
        }

        if (!fixed)
        {
            auto *mode = new QComboBox;
            mode->addItems({ tr("Проезд"), tr("Остановка") });
            table_->setCellWidget(r, ColMode, mode);

            auto *dwell = new QDoubleSpinBox;
            dwell->setRange(1.0, 60.0);
            dwell->setDecimals(0);
            dwell->setValue(1.0);
            table_->setCellWidget(r, ColDwell, dwell);

            auto *extra = new QDoubleSpinBox;
            extra->setRange(0.0, 120.0);
            extra->setDecimals(1);
            extra->setSingleStep(0.5);
            table_->setCellWidget(r, ColExtra, extra);

            connect(mode, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { recompute(); });
            connect(dwell, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this] { recompute(); });
            connect(extra, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this] { recompute(); });
        }
        else
        {
            auto *extra = new QDoubleSpinBox;
            extra->setRange(0.0, 0.0);
            extra->setEnabled(false);
            // Запас конечной станции - перегон до неё
            if (station == -2)
            {
                extra->setRange(0.0, 120.0);
                extra->setDecimals(1);
                extra->setSingleStep(0.5);
                extra->setEnabled(true);
                extra->setValue(state_.endExtraMin);
                connect(extra, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this] { recompute(); });
                table_->setCellWidget(r, ColExtra, extra);
            }
        }

        return r;
    };

    add_row(-1, state_.startName, 0.0, true);

    for (int i = 0; i < plan_.stations.size(); ++i)
    {
        const TripStation &st = plan_.stations[i];

        if (st.s < 30.0 || st.s > plan_.length - 30.0)
        {
            continue;
        }

        const int r = add_row(i, st.name, st.s, false);

        const StationSetting ss = state_.stations.value(st.name, StationSetting{false, false, 1, 0.0});

        table_->item(r, ColUse)->setCheckState(ss.included ? Qt::Checked : Qt::Unchecked);
        qobject_cast<QComboBox *>(table_->cellWidget(r, ColMode))->setCurrentIndex(ss.stop ? 1 : 0);
        qobject_cast<QDoubleSpinBox *>(table_->cellWidget(r, ColDwell))->setValue(ss.dwellMin);
        qobject_cast<QDoubleSpinBox *>(table_->cellWidget(r, ColExtra))->setValue(ss.extraMin);
    }

    add_row(-2, state_.endName, plan_.length, true);

}

//------------------------------------------------------------------------------
// Сохранить настройки из таблицы в состояние
//------------------------------------------------------------------------------
void TimetableDialog::readTable()
{
    for (int r = 0; r < table_->rowCount(); ++r)
    {
        const int st = row_station_.value(r, -1);

        if (st == -1)
        {
            state_.startName = table_->item(r, ColName)->text();
            continue;
        }

        if (st == -2)
        {
            state_.endName = table_->item(r, ColName)->text();

            if (auto *e = qobject_cast<QDoubleSpinBox *>(table_->cellWidget(r, ColExtra)))
            {
                state_.endExtraMin = e->value();
            }

            continue;
        }

        StationSetting ss;
        ss.included = table_->item(r, ColUse)->checkState() == Qt::Checked;
        ss.stop = qobject_cast<QComboBox *>(table_->cellWidget(r, ColMode))->currentIndex() == 1;
        ss.dwellMin = static_cast<int>(qobject_cast<QDoubleSpinBox *>(table_->cellWidget(r, ColDwell))->value());
        ss.extraMin = qobject_cast<QDoubleSpinBox *>(table_->cellWidget(r, ColExtra))->value();

        state_.stations.insert(plan_.stations[st].name, ss);
    }
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
TripOptions TimetableDialog::options() const
{
    TripOptions o;

    o.startTime = QTime(0, 0).secsTo(start_edit_->time());
    o.totalExtraMin = total_extra_->value();

    for (int r = 0; r < table_->rowCount(); ++r)
    {
        const int st = row_station_.value(r, -1);

        if (st == -1)
        {
            o.startName = table_->item(r, ColName)->text();
            continue;
        }

        if (st == -2)
        {
            o.endName = table_->item(r, ColName)->text();

            if (auto *e = qobject_cast<QDoubleSpinBox *>(table_->cellWidget(r, ColExtra)))
            {
                o.endExtraMin = e->value();
            }

            continue;
        }

        StopSpec sp;
        sp.station = st;

        if (table_->item(r, ColUse)->checkState() == Qt::Checked)
        {
            const bool stop = qobject_cast<QComboBox *>(table_->cellWidget(r, ColMode))->currentIndex() == 1;
            sp.mode = stop ? StopMode::Stop : StopMode::Pass;
        }
        else
        {
            sp.mode = StopMode::Skip;
        }

        sp.dwellSec = static_cast<int>(qobject_cast<QDoubleSpinBox *>(table_->cellWidget(r, ColDwell))->value()) * 60;
        sp.extraMin = qobject_cast<QDoubleSpinBox *>(table_->cellWidget(r, ColExtra))->value();
        o.stops.push_back(sp);
    }

    return o;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void TimetableDialog::recompute()
{
    if (loading_)
    {
        return;
    }

    if (!plan_.ok)
    {
        result_ = TripResult();
        showResult();
        return;
    }

    // Тяга из полей
    TripPlan plan = plan_;
    plan.dynamics.traction.powerKw = power_->value();
    plan.dynamics.traction.startForceKn = force_->value();
    plan.dynamics.traction.vMaxKmh = vmax_->value();
    plan.dynamics.traction.brakeDecel = brake_->value();

    // Первый расчёт: запас по умолчанию
    if (state_.totalExtraMin < 0.0 && table_->rowCount() > 0)
    {
        TripOptions probe = options();
        probe.totalExtraMin = 0.0;
        const TripResult base = computeTrip(plan, probe, route_, scenario_, train_index_);

        state_.totalExtraMin = base.ok ? defaultReserveMin(base.runTime) : 2.0;

        QSignalBlocker b(total_extra_);
        total_extra_->setValue(state_.totalExtraMin);
    }

    readTable();

    state_.powerKw = power_->value();
    state_.startForceKn = force_->value();
    state_.vMaxKmh = vmax_->value();
    state_.brakeDecel = brake_->value();
    state_.startTime = QTime(0, 0).secsTo(start_edit_->time());
    state_.totalExtraMin = total_extra_->value();

    result_ = computeTrip(plan, options(), route_, scenario_, train_index_);

    showResult();
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void TimetableDialog::showResult()
{
    QSignalBlocker block(table_);

    if (!result_.ok)
    {
        summary_->setText(QString());
        warnings_->setPlainText(plan_.ok ? result_.error : plan_.error);
        apply_button_->setEnabled(false);

        for (int r = 0; r < table_->rowCount(); ++r)
        {
            for (int c : { ColArrival, ColDeparture, ColSpeed })
            {
                table_->item(r, c)->setText(QString());
            }
        }

        return;
    }

    // Имена начала и конца по умолчанию
    for (int r = 0; r < table_->rowCount(); ++r)
    {
        const int st = row_station_.value(r, -1);
        QString name = table_->item(r, ColName)->text();

        const TripRow *row = nullptr;

        if (st == -1)
        {
            row = &result_.rows.first();
        }
        else if (st == -2)
        {
            row = &result_.rows.last();
        }
        else
        {
            for (const TripRow &tr : result_.rows)
            {
                if (std::abs(tr.s - plan_.stations[st].s) < 1e-6 && tr.name == plan_.stations[st].name)
                {
                    row = &tr;
                    break;
                }
            }
        }

        if (((st == -1) || (st == -2)) && name.isEmpty() && row != nullptr)
        {
            table_->item(r, ColName)->setText(row->name);
        }

        if (row == nullptr)
        {
            for (int c : { ColArrival, ColDeparture, ColSpeed })
            {
                table_->item(r, c)->setText(QStringLiteral("—"));
            }

            continue;
        }

        const bool first = (st == -1);
        const bool last = (st == -2);

        table_->item(r, ColArrival)->setText(first ? QString() : formatClock(row->arrival));
        table_->item(r, ColDeparture)->setText(last ? QString() : formatClock(row->departure));
        table_->item(r, ColSpeed)->setText(QString::number(row->speedKmh, 'f', 0));
    }

    const TripRow &end = result_.rows.last();

    summary_->setText(tr("Длина пути %1 км. Время хода без запаса %2. Прибытие в %3 (в пути %4 "
                         "с учётом стоянок и запаса).")
                      .arg(plan_.length / 1000.0, 0, 'f', 1)
                      .arg(minutesText(result_.runTime))
                      .arg(formatClock(end.arrival))
                      .arg(minutesText(end.arrival - result_.timetable.startTime)));

    warnings_->setPlainText(result_.warnings.join(QLatin1Char('\n')));
    apply_button_->setEnabled(true);
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
TimetableState TimetableDialog::state() const
{
    return state_;
}

QString TimetableDialog::warningsText() const
{
    return warnings_->toPlainText();
}

QString TimetableDialog::summaryText() const
{
    return summary_->text();
}

} // namespace rse
