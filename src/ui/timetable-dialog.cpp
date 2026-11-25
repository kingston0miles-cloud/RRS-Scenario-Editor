#include    "ui/timetable-dialog.h"
#include    "scenario/autotriggers.h"
#include    "scenario/export.h"
#include    "scenario/stationroute.h"

#include    <QComboBox>
#include    <QButtonGroup>
#include    <QDoubleSpinBox>
#include    <QFormLayout>
#include    <QGroupBox>
#include    <QHBoxLayout>
#include    <QHeaderView>
#include    <QLabel>
#include    <QListWidget>
#include    <QPlainTextEdit>
#include    <QPushButton>
#include    <QSet>
#include    <QSignalBlocker>
#include    <QTableWidget>
#include    <QTimeEdit>
#include    <QToolButton>
#include    <QVBoxLayout>

#include    <algorithm>
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

/// Время в виде ЧЧ:ММ (столбцы прибытия и отправления редактируемые)
QString clockText(int sec)
{
    sec = ((sec % 86400) + 86400) % 86400;

    return QStringLiteral("%1:%2")
            .arg(sec / 3600, 2, 10, QLatin1Char('0'))
            .arg((sec / 60) % 60, 2, 10, QLatin1Char('0'));
}

/// Разбор времени «Ч», «Ч:М», «Ч:ММ:СС». ok=false - не распознано;
/// пустая строка - очистка ручного времени (ok=true, результат -1)
int parseClockText(const QString &text, bool *ok)
{
    const QString t = text.trimmed();

    *ok = false;

    if (t.isEmpty())
    {
        *ok = true;
        return -1;
    }

    const QStringList parts = t.split(QLatin1Char(':'));

    if (parts.isEmpty() || parts.size() > 3)
    {
        return -1;
    }

    qint64 v[3] = {0, 0, 0};

    for (int i = 0; i < parts.size(); ++i)
    {
        bool num = false;
        v[i] = parts[i].toLongLong(&num);

        if (!num || v[i] < 0)
        {
            return -1;
        }
    }

    if ((v[0] > 47) || (parts.size() > 1 && v[1] > 59) || (parts.size() > 2 && v[2] > 59))
    {
        return -1;
    }

    *ok = true;

    return static_cast<int>(v[0] * 3600 + v[1] * 60 + v[2]);
}

/// Когда поезд впервые вступит на траекторию по расчётным точкам движения
/// (fallback - если траектории в точках нет)
int enterTime(const QVector<TimetableSample> &samples, const QString &traj, int fallback)
{
    for (const TimetableSample &s : samples)
    {
        if (s.trajectory == traj)
        {
            return s.time;
        }
    }

    return fallback;
}

/// Значок важности строки замечания (аудит 3.3): противоречия времён - «⚠»,
/// прочие замечания - «ℹ»
QString warnIcon(const QString &line)
{
    if (line.contains(QStringLiteral("раньше")) || line.contains(QStringLiteral("поправьте")))
    {
        return QStringLiteral("⚠ ") + line;
    }

    return QStringLiteral("ℹ ") + line;
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
            st.endManualArrival = s.manualArrival ? s.arrival : -1;
            continue;
        }

        StationSetting ss;
        ss.included = true;
        ss.stop = s.stops();
        ss.dwellMin = std::max(1, (s.departure - s.arrival) / 60);
        ss.extraMin = s.extraMin;
        ss.track = s.targetTrajectory;
        ss.trackCoord = s.targetCoord;
        ss.manualArrival = s.manualArrival ? s.arrival : -1;
        ss.manualDeparture = s.manualDeparture ? s.departure : -1;
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

    // Закреплённые промежуточные станции: путь строится «через станцию»
    pin_combo_ = new QComboBox(path_box);

    for (const StationData &sd : route_.stations())
    {
        pin_combo_->addItem(sd.name);
    }

    pin_button_ = new QPushButton(tr("Закрепить"), path_box);
    pin_button_->setEnabled(!route_.stations().isEmpty());
    connect(pin_button_, &QPushButton::clicked, this, &TimetableDialog::onPinButton);
    connect(pin_combo_, &QComboBox::currentTextChanged,
            this, [this] { refreshPinControls(); });

    pin_list_ = new QListWidget(path_box);
    pin_list_->setMaximumHeight(64);
    pin_list_->setToolTip(tr("Путь поезда пройдёт через эти станции по порядку. "
                             "Путь станции выбирается в таблице"));
    connect(pin_list_, &QListWidget::itemSelectionChanged,
            this, &TimetableDialog::onPinSelected);

    auto *pin_row = new QHBoxLayout;
    pin_row->addWidget(pin_combo_, 1);
    pin_row->addWidget(pin_button_);
    path_form->addRow(tr("Через:"), pin_row);
    path_form->addRow(QString(), pin_list_);

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

    // Категория поезда (аудит 3.8)
    class_box_ = new QComboBox(path_box);

    for (int c = 0; c <= static_cast<int>(TrainClass::Service); ++c)
    {
        class_box_->addItem(trainClassTitle(static_cast<TrainClass>(c)), c);
    }

    class_box_->setCurrentIndex(std::clamp(train.trainClass, 0, static_cast<int>(TrainClass::Service)));
    class_box_->setToolTip(tr("Определяет очерёдность пропуска: поезд низшей категории не "
                              "получит маршрут раньше высшей, если графики спорят за один путь"));
    path_form->addRow(tr("Категория:"), class_box_);

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

    // Ручные времена: возврат строк к авторасчёту; режим таблицы (аудит 3.5)
    auto *recalc_row = new QHBoxLayout;
    recalc_row->addWidget(new QLabel(tr("Ручные времена:"), this));
    recalc_next_ = new QPushButton(tr("Пересчитать далее"), this);
    recalc_next_->setToolTip(tr("Со строки с курсором и ниже: вернуть время к авторасчёту"));
    recalc_all_ = new QPushButton(tr("Пересчитать всё"), this);
    recalc_all_->setToolTip(tr("Вернуть все строки графика к авторасчёту"));
    recalc_row->addWidget(recalc_next_);
    recalc_row->addWidget(recalc_all_);
    recalc_row->addStretch(1);

    simple_mode_ = false;
    auto *simple_btn = new QToolButton(this);
    simple_btn->setText(tr("Диспетчер"));
    simple_btn->setCheckable(true);
    simple_btn->setToolTip(tr("Только станции и времена - без стоянок, запасов и путей"));

    auto *fine_btn = new QToolButton(this);
    fine_btn->setText(tr("Тонкая настройка"));
    fine_btn->setCheckable(true);
    fine_btn->setChecked(true);

    auto *mode_group = new QButtonGroup(this);
    mode_group->addButton(simple_btn, 0);
    mode_group->addButton(fine_btn, 1);
    mode_group->setExclusive(true);
    recalc_row->addWidget(new QLabel(tr("Таблица:"), this));
    recalc_row->addWidget(simple_btn);
    recalc_row->addWidget(fine_btn);
    layout->addLayout(recalc_row);

    connect(mode_group, &QButtonGroup::idClicked, this, [this](int id)
    {
        setSimpleMode(id == 0);
    });

    // Таблица
    table_ = new QTableWidget(this);
    table_->setColumnCount(ColCount);
    table_->setHorizontalHeaderLabels({ tr("В графике"), tr("Станция"), tr("км"), tr("Путь станции"), tr("Режим"),
                                        tr("Стоянка, мин"), tr("Запас до станции, мин"),
                                        tr("Прибытие"), tr("Отправление"), tr("Маршрут"), tr("Скорость, км/ч") });
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(ColName, QHeaderView::Stretch);
    table_->verticalHeader()->hide();
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(table_, 1);

    summary_ = new QLabel(this);
    summary_->setWordWrap(true);
    layout->addWidget(summary_);

    warnings_ = new QPlainTextEdit(this);
    warnings_->setReadOnly(true);
    warnings_->setMaximumHeight(90);
    warnings_->setStyleSheet(QStringLiteral("color: #a02020;"));
    layout->addWidget(warnings_);

    // Статус «изменения ещё не применены» (аудит 3.4)
    state_label_ = new QLabel(this);
    state_label_->setWordWrap(true);
    state_label_->setStyleSheet(QStringLiteral("color: #a35a00;"));
    state_label_->hide();
    layout->addWidget(state_label_);

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

    connect(table_, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *item)
    {
        if (loading_)
        {
            return;
        }

        // Время прибытия/отправления правится прямо в ячейке
        if (item->column() == ColArrival || item->column() == ColDeparture)
        {
            parseManualTime(item);
            return;
        }

        markDirty();
        recompute();
    });
    connect(pick, &QPushButton::clicked, this, [this] { emit pickRequested(); });
    connect(remove, &QPushButton::clicked, this, [this] { done(RemoveRequested); });
    connect(apply_button_, &QPushButton::clicked, this, &QDialog::accept);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(def_button, &QPushButton::clicked, this, &TimetableDialog::onDefaultReserve);
    connect(recalc_next_, &QPushButton::clicked, this, &TimetableDialog::onRecalcNext);
    connect(recalc_all_, &QPushButton::clicked, this, &TimetableDialog::onRecalcAll);
    connect(waypoint_combo_, QOverload<int>::of(&QComboBox::activated),
            this, &TimetableDialog::onWaypointChosen);

    connect(start_edit_, &QTimeEdit::timeChanged, this, [this]
    {
        markDirty();
        recompute();
    });
    connect(total_extra_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this]
    {
        markDirty();
        recompute();
    });

    for (QDoubleSpinBox *s : { power_, force_, vmax_, brake_ })
    {
        connect(s, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this]
        {
            if (!loading_)
            {
                markEdited();
                markDirty();
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
    markDirty();
    rebuildPlan();
}

//------------------------------------------------------------------------------
// Режим таблицы «Диспетчер»: только станции и времена (аудит 3.5)
//------------------------------------------------------------------------------
void TimetableDialog::setSimpleMode(bool simple)
{
    simple_mode_ = simple;

    for (int c : { ColUse, ColTrack, ColMode, ColDwell, ColExtra })
    {
        table_->setColumnHidden(c, simple);
    }
}

//------------------------------------------------------------------------------
// Категория поезда, выбранная в окне (аудит 3.8)
//------------------------------------------------------------------------------
int TimetableDialog::category() const
{
    return class_box_ != nullptr ? class_box_->currentData().toInt() : 0;
}

//------------------------------------------------------------------------------
// Изменения ещё не применены (аудит 3.4)
//------------------------------------------------------------------------------
void TimetableDialog::markDirty()
{
    dirty_ = true;
    updateDirtyLabel();
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void TimetableDialog::updateDirtyLabel()
{
    state_label_->setText(tr("Изменения ещё не применены — нажмите «Применить», чтобы они "
                             "попали в проект и в экспорт"));
    state_label_->setVisible(dirty_);
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

    // Закреплённые станции задают порядок трассы («через станцию»), затем -
    // выбранные пути прочих станций по прежнему плану
    QVector<TripVia> vias;
    QSet<QString> pinned;
    QStringList pin_errors;

    for (const TripPin &p : state_.pins)
    {
        const StationSetting ss = state_.stations.value(p.station);
        TripVia v;

        if (pinToPoint(route_, p.station,
                       ss.track.isEmpty() ? p.track : ss.track,
                       ss.track.isEmpty() ? p.trackCoord : ss.trackCoord, &v))
        {
            vias.push_back(v);
            pinned.insert(p.station);
        }
        else
        {
            pin_errors << tr("закреплённая станция «%1»: путь не найден").arg(p.station);
        }
    }

    for (const TripStation &ts : plan_.stations)
    {
        const StationSetting ss = state_.stations.value(ts.name);

        if (!ss.track.isEmpty() && !pinned.contains(ts.name))
        {
            vias.push_back({ ss.track, ss.trackCoord });
        }
    }

    plan_ = TripPlan();
    result_ = TripResult();

    if (state_.hasEnd)
    {
        plan_ = buildTripPlan(route_, catalog_, scenario_, train_index_,
                              state_.endTrajectory, state_.endCoord, vias);
        plan_.warnings << pin_errors;
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
    refreshPinControls();
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

        for (int c : { ColArrival, ColDeparture, ColRoute, ColSpeed })
        {
            auto *it = new QTableWidgetItem;
            it->setTextAlignment(Qt::AlignCenter);
            table_->setItem(r, c, it);

            // Время прибытия/отправления правится прямо в ячейке
            if (c == ColArrival || c == ColDeparture)
            {
                it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsEditable);
                it->setToolTip(tr("Впишите время ЧЧ:ММ, чтобы закрепить его "
                                  "(очистите ячейку - вернётся авторасчёт)"));
            }
            else
            {
                it->setFlags(Qt::ItemIsEnabled);
            }
        }

        if (!fixed)
        {
            auto *track = new QComboBox;
            track->addItem(tr("авто"));
            table_->setCellWidget(r, ColTrack, track);
            connect(track, QOverload<int>::of(&QComboBox::activated), this, [this] { onTrackChosen(); });

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

            connect(mode, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]
            {
                markDirty();
                recompute();
            });
            connect(dwell, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this]
            {
                markDirty();
                recompute();
            });
            connect(extra, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this]
            {
                markDirty();
                recompute();
            });
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
                connect(extra, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this]
                {
                    markDirty();
                    recompute();
                });
                table_->setCellWidget(r, ColExtra, extra);
            }
        }

        // Крайние строки: время начала задается полем «Отправление»,
        // у конечной станции правится только прибытие
        if (fixed)
        {
            if (station == -1)
            {
                table_->item(r, ColArrival)->setFlags(Qt::ItemIsEnabled);
                table_->item(r, ColDeparture)->setFlags(Qt::ItemIsEnabled);
            }
            else
            {
                table_->item(r, ColDeparture)->setFlags(Qt::ItemIsEnabled);
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

        if (auto *track = qobject_cast<QComboBox *>(table_->cellWidget(r, ColTrack)))
        {
            for (const StationData &sd : route_.stations())
            {
                if (sd.name != st.name)
                {
                    continue;
                }

                for (const StationTrack &t : stationTracks(route_, sd.pos))
                {
                    track->addItem(t.trajectory, t.trajectory);
                    track->setItemData(track->count() - 1, t.coord, Qt::UserRole + 1);

                    if (t.trajectory == ss.track)
                    {
                        track->setCurrentIndex(track->count() - 1);
                    }
                }

                break;
            }
        }

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

        // Ручные времена меняются только правкой ячеек времени - сохраняются
        const StationSetting old = state_.stations.value(plan_.stations[st].name);

        StationSetting ss;
        ss.included = table_->item(r, ColUse)->checkState() == Qt::Checked;
        ss.stop = qobject_cast<QComboBox *>(table_->cellWidget(r, ColMode))->currentIndex() == 1;
        ss.dwellMin = static_cast<int>(qobject_cast<QDoubleSpinBox *>(table_->cellWidget(r, ColDwell))->value());
        ss.extraMin = qobject_cast<QDoubleSpinBox *>(table_->cellWidget(r, ColExtra))->value();
        ss.manualArrival = old.manualArrival;
        ss.manualDeparture = old.manualDeparture;

        if (auto *track = qobject_cast<QComboBox *>(table_->cellWidget(r, ColTrack)))
        {
            if (track->currentIndex() > 0)
            {
                ss.track = track->currentData().toString();
                ss.trackCoord = track->currentData(Qt::UserRole + 1).toDouble();
            }
        }

        state_.stations.insert(plan_.stations[st].name, ss);
    }
}

//------------------------------------------------------------------------------
// Выбран другой путь станции: маршрут перестраивается через него
//------------------------------------------------------------------------------
void TimetableDialog::onTrackChosen()
{
    markDirty();
    readTable();
    rebuildPlan();
}

//------------------------------------------------------------------------------
// Время, вписанное в ячейку прибытия/отправления: строка становится ручной,
// последующие строки авторасчёта пересчитываются от неё (якорь)
//------------------------------------------------------------------------------
void TimetableDialog::parseManualTime(QTableWidgetItem *item)
{
    const int r = item->row();
    const int st = row_station_.value(r, -99);

    bool ok = false;
    const int sec = parseClockText(item->text(), &ok);

    // Начало: время отправления задается полем «Отправление»;
    // нераспознанное значение - вернуть прежний текст
    if (st == -1 || !ok || (st == -2 && item->column() == ColDeparture))
    {
        recompute();
        return;
    }

    if (st == -2)
    {
        state_.endManualArrival = sec;
        markDirty();
        recompute();
        return;
    }

    if (st < 0 || st >= plan_.stations.size())
    {
        return;
    }

    // Правка времени в выключенной строке включает её в график
    if (table_->item(r, ColUse)->checkState() != Qt::Checked)
    {
        table_->item(r, ColUse)->setCheckState(Qt::Checked);
    }

    const QString name = plan_.stations[st].name;
    StationSetting ss = state_.stations.value(name);

    if (item->column() == ColArrival)
    {
        ss.manualArrival = sec;
    }
    else
    {
        ss.manualDeparture = sec;
    }

    state_.stations.insert(name, ss);
    markDirty();
    recompute();
}

//------------------------------------------------------------------------------
// Вернуть строку к авторасчёту
//------------------------------------------------------------------------------
bool TimetableDialog::clearManualRow(int row, int station)
{
    if (station == -2)
    {
        if (state_.endManualArrival < 0)
        {
            return false;
        }

        state_.endManualArrival = -1;
        return true;
    }

    if (station < 0 || station >= plan_.stations.size())
    {
        return false;
    }

    const QString name = plan_.stations[station].name;
    StationSetting ss = state_.stations.value(name);

    if (ss.manualArrival < 0 && ss.manualDeparture < 0)
    {
        return false;
    }

    ss.manualArrival = -1;
    ss.manualDeparture = -1;
    state_.stations.insert(name, ss);

    return true;
}

//------------------------------------------------------------------------------
// «Пересчитать далее»: от строки с курсором и ниже - к авторасчёту
//------------------------------------------------------------------------------
void TimetableDialog::onRecalcNext()
{
    const int r0 = std::max(0, table_->currentRow());
    bool changed = false;

    for (int r = 0; r < table_->rowCount(); ++r)
    {
        if (r >= r0)
        {
            changed |= clearManualRow(r, row_station_.value(r, -99));
        }
    }

    if (changed)
    {
        markDirty();
        recompute();
    }
}

//------------------------------------------------------------------------------
// «Пересчитать всё»: весь график - к авторасчёту
//------------------------------------------------------------------------------
void TimetableDialog::onRecalcAll()
{
    bool changed = false;

    for (int r = 0; r < table_->rowCount(); ++r)
    {
        changed |= clearManualRow(r, row_station_.value(r, -99));
    }

    if (changed)
    {
        markDirty();
        recompute();
    }
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
int TimetableDialog::pinIndex(const QString &station) const
{
    for (int i = 0; i < state_.pins.size(); ++i)
    {
        if (state_.pins[i].station == station)
        {
            return i;
        }
    }

    return -1;
}

//------------------------------------------------------------------------------
// Кнопка «Закрепить/Убрать» у выбранной в списке станции
//------------------------------------------------------------------------------
void TimetableDialog::onPinButton()
{
    const QString name = pin_combo_->currentText();

    if (name.isEmpty())
    {
        return;
    }

    const int idx = pinIndex(name);

    if (idx >= 0)
    {
        state_.pins.removeAt(idx);
    }
    else
    {
        TripPin p;
        p.station = name;
        state_.pins.push_back(p);
    }

    markDirty();
    rebuildPlan();
}

//------------------------------------------------------------------------------
// Щелчок по закреплённой станции в списке: выбрать её в списке станций
//------------------------------------------------------------------------------
void TimetableDialog::onPinSelected()
{
    QListWidgetItem *it = pin_list_->currentItem();

    if (it != nullptr)
    {
        pin_combo_->setCurrentText(it->data(Qt::UserRole).toString());
    }
}

//------------------------------------------------------------------------------
// Список закреплённых станций и подпись кнопки «Закрепить/Убрать»
//------------------------------------------------------------------------------
void TimetableDialog::refreshPinControls()
{
    QSignalBlocker block(pin_list_);

    pin_list_->clear();

    for (const TripPin &p : state_.pins)
    {
        const StationSetting ss = state_.stations.value(p.station);
        QString text = p.station;

        if (!ss.track.isEmpty())
        {
            text += tr(" — путь «%1»").arg(ss.track);
        }

        auto *item = new QListWidgetItem(text, pin_list_);
        item->setData(Qt::UserRole, p.station);
    }

    pin_list_->setVisible(!state_.pins.isEmpty());
    pin_button_->setText(pinIndex(pin_combo_->currentText()) >= 0 ? tr("Убрать")
                                                                  : tr("Закрепить"));
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

            o.endManualArrival = state_.endManualArrival;

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

        const StationSetting ss = state_.stations.value(plan_.stations[st].name);
        sp.manualArrival = ss.manualArrival;
        sp.manualDeparture = ss.manualDeparture;

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
// Маршруты, которые построит автопилот игры по рассчитанному расписанию, и
// светофоры, которые он откроет. Подход и уход берутся так же, как при экспорте
//------------------------------------------------------------------------------
void TimetableDialog::updateRouteReport()
{
    route_report_ = AutopilotReport();
    route_approach_.clear();
    route_removal_.clear();

    const QVector<TimetableStation> &sts = result_.timetable.stations;

    if (sts.size() < 2)
    {
        return;
    }

    PlacedTrain probe = scenario_.trains()[train_index_];
    probe.timetable = result_.timetable;

    timetableApproachRemoval(route_, probe, &route_approach_, &route_removal_);

    QVector<AutoStation> as;

    for (int k = 0; k < sts.size(); ++k)
    {
        AutoStation a;

        a.name = sts[k].name;
        a.target = sts[k].targetTrajectory;
        a.approach = sts[k].approachTrajectory.isEmpty() ? route_approach_.value(k)
                                                         : sts[k].approachTrajectory;
        a.removal = sts[k].removalTrajectory.isEmpty() ? route_removal_.value(k)
                                                       : sts[k].removalTrajectory;
        as.push_back(a);
    }

    route_report_ = simulateAutopilot(route_, as);
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
        warnings_->setPlainText(QStringLiteral("✖ ") + (plan_.ok ? result_.error : plan_.error));
        apply_button_->setEnabled(false);
        emit tripChanged({}, {});
        emit signalMarksChanged({});

        for (int r = 0; r < table_->rowCount(); ++r)
        {
            for (int c : { ColArrival, ColDeparture, ColRoute, ColSpeed })
            {
                table_->item(r, c)->setText(QString());
                table_->item(r, c)->setToolTip(QString());
                table_->item(r, c)->setBackground(QBrush());
            }
        }

        return;
    }

    // Как выполнит график автопилот игры: столбец «Маршрут» и подсветка светофоров
    updateRouteReport();

    const QColor normal_color = table_->palette().color(QPalette::WindowText);
    const QColor red(170, 30, 30);
    QVector<SignalMark> marks;

    auto fill_route = [&](int r, int k)
    {
        QTableWidgetItem *cell = table_->item(r, ColRoute);
        QTableWidgetItem *name_cell = table_->item(r, ColName);
        const QVector<TimetableStation> &sts = result_.timetable.stations;

        if (route_report_.dir == 0 || route_report_.rows.isEmpty() || k < 0 || k >= sts.size())
        {
            cell->setText(tr("не построится"));
            cell->setForeground(red);
            cell->setToolTip(route_report_.problems.join(QLatin1Char('\n')));
            name_cell->setForeground(red);
            return;
        }

        const StationRouteCheck &rc = route_report_.rows[k];

        QStringList parts;

        // Приём: маршрут строится, когда поезд вступает на подход
        if (k > 0)
        {
            if (!rc.arrivalOk)
            {
                parts << tr("приём не построится");
            }
            else if (rc.arrivalSignals.isEmpty())
            {
                parts << tr("приём: светофоров нет");
            }
            else
            {
                const int t = enterTime(result_.timetable.samples, route_approach_.value(k),
                                        sts[k].arrival);
                parts << tr("приём «%1» в %2").arg(rc.arrivalSignals.join(QStringLiteral(", ")),
                                                   formatClock(t));
            }
        }

        // Отправление: маршрут открывается ко времени отправления по графику
        if (k + 1 < sts.size())
        {
            if (!rc.departureOk)
            {
                parts << tr("отправление не построится");
            }
            else if (rc.departureSignals.isEmpty())
            {
                parts << tr("отправление: светофоров нет");
            }
            else
            {
                parts << tr("отправление «%1» в %2")
                            .arg(rc.departureSignals.join(QStringLiteral(", ")),
                                 formatClock(sts[k].departure));
            }
        }

        cell->setText(parts.join(QStringLiteral("; ")));

        const bool bad = !rc.arrivalOk || !rc.departureOk || !rc.problems.isEmpty();

        cell->setForeground(bad ? red : normal_color);
        name_cell->setForeground(bad ? red : normal_color);
        cell->setToolTip(rc.problems.isEmpty()
                         ? tr("Оценка: маршрут приёма строится при вступлении на подход, "
                              "отправления - ко времени отправления по графику")
                         : rc.problems.join(QLatin1Char('\n')));

        for (const RouteSignalMark &m : rc.arrivalMarks)
        {
            SignalMark s;

            s.sw = m.sw;
            s.liter = m.liter;
            s.station = m.station;
            s.problem = bad;
            marks.push_back(s);
        }

        for (const RouteSignalMark &m : rc.departureMarks)
        {
            SignalMark s;

            s.sw = m.sw;
            s.liter = m.liter;
            s.station = m.station;
            s.problem = bad;
            marks.push_back(s);
        }
    };

    // Имена начала и конца по умолчанию
    for (int r = 0; r < table_->rowCount(); ++r)
    {
        const int st = row_station_.value(r, -1);
        QString name = table_->item(r, ColName)->text();

        const TripRow *row = nullptr;
        int k = -1;
        int idx = 0;

        if (st == -1)
        {
            row = &result_.rows.first();
            k = 0;
        }
        else if (st == -2)
        {
            row = &result_.rows.last();
            k = result_.rows.size() - 1;
        }
        else
        {
            for (const TripRow &tr : result_.rows)
            {
                if (std::abs(tr.s - plan_.stations[st].s) < 1e-6 && tr.name == plan_.stations[st].name)
                {
                    row = &tr;
                    k = idx;
                    break;
                }

                ++idx;
            }
        }

        if (((st == -1) || (st == -2)) && name.isEmpty() && row != nullptr)
        {
            table_->item(r, ColName)->setText(row->name);
        }

        if (row == nullptr)
        {
            for (int c : { ColArrival, ColDeparture, ColRoute, ColSpeed })
            {
                table_->item(r, c)->setText(QStringLiteral("—"));
                table_->item(r, c)->setBackground(QBrush());
                table_->item(r, c)->setForeground(normal_color);
            }

            continue;
        }

        const bool first = (st == -1);
        const bool last = (st == -2);

        QTableWidgetItem *arr_cell = table_->item(r, ColArrival);
        QTableWidgetItem *dep_cell = table_->item(r, ColDeparture);

        arr_cell->setText(first ? QString() : clockText(row->arrival));
        dep_cell->setText(last ? QString() : clockText(row->departure));
        table_->item(r, ColSpeed)->setText(QString::number(row->speedKmh, 'f', 0));

        // Ручные времена подсвечиваются; противоречивые строки - красным
        const QColor manual_bg(255, 246, 205);
        const bool times_bad = (row->departure < row->arrival)
                || (k > 0 && row->arrival < result_.rows[k - 1].departure);

        arr_cell->setBackground(row->manualArrival ? QBrush(manual_bg) : QBrush());
        dep_cell->setBackground(row->manualDeparture ? QBrush(manual_bg) : QBrush());

        if (times_bad)
        {
            arr_cell->setForeground(red);
            dep_cell->setForeground(red);
            arr_cell->setToolTip(tr("Время противоречит соседним строкам графика"));
        }
        else
        {
            arr_cell->setForeground(normal_color);
            dep_cell->setForeground(normal_color);
        }

        fill_route(r, k);
    }

    const TripRow &end = result_.rows.last();

    summary_->setText(tr("Длина пути %1 км. Время хода без запаса %2. Прибытие в %3 (в пути %4 "
                         "с учётом стоянок и запаса).")
                      .arg(plan_.length / 1000.0, 0, 'f', 1)
                      .arg(minutesText(result_.runTime))
                      .arg(formatClock(end.arrival))
                      .arg(minutesText(end.arrival - result_.timetable.startTime)));

    QStringList warned;

    for (const QString &line : result_.warnings)
    {
        warned << warnIcon(line);
    }

    warnings_->setPlainText(warned.join(QLatin1Char('\n')));
    apply_button_->setEnabled(true);
    emit signalMarksChanged(marks);
    emit tripChanged(plan_.path.segments, result_.timetable.stations);
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
