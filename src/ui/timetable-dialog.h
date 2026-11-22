#ifndef     RSE_TIMETABLE_DIALOG_H
#define     RSE_TIMETABLE_DIALOG_H

#include    <QDialog>
#include    <QHash>

#include    "catalog/vehicle-catalog.h"
#include    "scenario/trip.h"

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QTableWidget;
class QTimeEdit;

namespace rse
{

//------------------------------------------------------------------------------
// Настройки станции в графике (сохраняются между открытиями диалога)
//------------------------------------------------------------------------------
struct StationSetting
{
    bool included = true;
    bool stop = false;
    int dwellMin = 1;
    double extraMin = 0.0;
};

//------------------------------------------------------------------------------
// Состояние диалога графика: что выбрал пользователь
//------------------------------------------------------------------------------
struct TimetableState
{
    bool hasEnd = false;
    QString endTrajectory;
    double endCoord = 0.0;

    int startTime = 12 * 3600;

    /// Запас на весь путь, мин; отрицательное значение - по умолчанию (10%)
    double totalExtraMin = -1.0;

    QString startName;
    QString endName;
    double endExtraMin = 0.0;

    QHash<QString, StationSetting> stations;

    /// Тяга, поправленная пользователем
    bool tractionEdited = false;
    double powerKw = 0.0;
    double startForceKn = 0.0;
    double vMaxKmh = 0.0;
    double brakeDecel = 0.5;

    /// Состояние по уже имеющемуся расписанию поезда
    static TimetableState fromTimetable(const Timetable &tt);
};

//------------------------------------------------------------------------------
// График поезда: конечная точка, станции по пути (остановка или проезд),
// запас времени на весь путь и на перегон до станции. Время пересчитывается
// сразу после каждого изменения
//------------------------------------------------------------------------------
class TimetableDialog : public QDialog
{
    Q_OBJECT

public:

    /// Коды завершения, кроме Accepted/Rejected
    enum Result
    {
        PickRequested = 2,
        RemoveRequested = 3
    };

    TimetableDialog(const Route &route, const VehicleCatalog &catalog, const Scenario &scenario,
                    int train_index, const TimetableState &state, QWidget *parent = nullptr);

    /// Что выбрал пользователь (для повторного открытия)
    TimetableState state() const;

    /// Рассчитанное расписание (valid == false, если расчёт не удался)
    Timetable timetable() const
    {
        return result_.timetable;
    }

    const TripResult &result() const
    {
        return result_;
    }

    const TripPlan &plan() const
    {
        return plan_;
    }

    /// Для проверок
    QTableWidget *table() const
    {
        return table_;
    }

    QPushButton *applyButton() const
    {
        return apply_button_;
    }

    QDoubleSpinBox *totalExtraSpin() const
    {
        return total_extra_;
    }

    QTimeEdit *startEdit() const
    {
        return start_edit_;
    }

    QString warningsText() const;
    QString summaryText() const;

    /// Столбцы таблицы
    enum Column
    {
        ColUse,
        ColName,
        ColKm,
        ColMode,
        ColDwell,
        ColExtra,
        ColArrival,
        ColDeparture,
        ColSpeed,
        ColCount
    };

public slots:

    /// Задать конечную точку
    void setEnd(const QString &trajectory, double coord);

private slots:

    void onWaypointChosen(int index);
    void onDefaultReserve();
    void recompute();

private:

    const Route &route_;
    const VehicleCatalog &catalog_;
    const Scenario &scenario_;
    int train_index_ = -1;

    TimetableState state_;
    TripPlan plan_;
    TripResult result_;

    bool loading_ = false;

    QLabel *end_label_ = nullptr;
    QComboBox *waypoint_combo_ = nullptr;
    QTimeEdit *start_edit_ = nullptr;
    QDoubleSpinBox *total_extra_ = nullptr;
    QDoubleSpinBox *power_ = nullptr;
    QDoubleSpinBox *force_ = nullptr;
    QDoubleSpinBox *vmax_ = nullptr;
    QDoubleSpinBox *brake_ = nullptr;
    QLabel *traction_note_ = nullptr;
    QTableWidget *table_ = nullptr;
    QLabel *summary_ = nullptr;
    QPlainTextEdit *warnings_ = nullptr;
    QPushButton *apply_button_ = nullptr;

    /// Номер станции плана для строки таблицы (-1 начало, -2 конец)
    QVector<int> row_station_;

    void rebuildPlan();
    void fillTable();
    void readTable();
    void showResult();
    void markEdited();
    TripOptions options() const;
};

} // namespace rse

#endif // RSE_TIMETABLE_DIALOG_H
