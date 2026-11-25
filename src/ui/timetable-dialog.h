#ifndef     RSE_TIMETABLE_DIALOG_H
#define     RSE_TIMETABLE_DIALOG_H

#include    <QDialog>
#include    <QHash>

#include    "catalog/vehicle-catalog.h"
#include    "scenario/stationroute.h"
#include    "scenario/trip.h"
#include    "ui/map-widget.h"

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QTableWidget;
class QTableWidgetItem;
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

    /// Выбранный путь станции (пусто - автоматически)
    QString track;
    double trackCoord = 0.0;

    /// Ручные времена строки, с от начала суток (-1 - авторасчёт)
    int manualArrival = -1;
    int manualDeparture = -1;
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

    /// Ручное время прибытия на конечную станцию (-1 - авторасчёт)
    int endManualArrival = -1;

    QHash<QString, StationSetting> stations;

    /// Закреплённые промежуточные станции трассы («через станцию»), по порядку
    QVector<TripPin> pins;

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

signals:
    /// Пользователь просит выбрать конец пути на карте (окно остаётся открытым)
    void pickRequested();

    /// Маршрут изменился: перерисовать на карте
    void tripChanged(const QVector<PathSegment> &path, const QVector<TimetableStation> &stations);

    /// Светофоры, которые откроются маршрутами графика: подсветить на карте
    /// (пусто - убрать подсветку)
    void signalMarksChanged(const QVector<SignalMark> &marks);

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

    /// Для проверок: возврат к авторасчёту
    QPushButton *recalcNextButton() const
    {
        return recalc_next_;
    }

    QPushButton *recalcAllButton() const
    {
        return recalc_all_;
    }

    /// Для проверок: закреплённые станции трассы
    QComboBox *pinCombo() const
    {
        return pin_combo_;
    }

    QPushButton *pinButton() const
    {
        return pin_button_;
    }

    QListWidget *pinList() const
    {
        return pin_list_;
    }

    /// Режим таблицы (аудит 3.5): true - «Диспетчер» (только станции и времена),
    /// false - «Тонкая настройка» (все столбцы)
    bool simpleMode() const
    {
        return simple_mode_;
    }

    void setSimpleMode(bool simple);

    /// Есть изменения, которые ещё не применены кнопкой «Применить» (аудит 3.4)
    bool isDirty() const
    {
        return dirty_;
    }

    /// Категория поезда, выбранная в окне (аудит 3.8)
    int category() const;

    QString warningsText() const;
    QString summaryText() const;

    /// Столбцы таблицы
    enum Column
    {
        ColUse,
        ColName,
        ColKm,
        ColTrack,
        ColMode,
        ColDwell,
        ColExtra,
        ColArrival,
        ColDeparture,
        ColRoute,
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
    void onTrackChosen();
    void onPinButton();
    void onPinSelected();
    void onRecalcNext();
    void onRecalcAll();

private:

    const Route &route_;
    const VehicleCatalog &catalog_;
    const Scenario &scenario_;
    int train_index_ = -1;

    TimetableState state_;
    TripPlan plan_;
    TripResult result_;

    /// Как выполнит график автопилот игры: маршруты станций и их светофоры
    /// (для столбца «Маршрут» и подсветки светофоров на карте)
    AutopilotReport route_report_;
    QStringList route_approach_;
    QStringList route_removal_;

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

    /// Закреплённые станции трассы («через станцию»)
    QComboBox *pin_combo_ = nullptr;
    QPushButton *pin_button_ = nullptr;
    QListWidget *pin_list_ = nullptr;

    /// Возврат строк к авторасчёту
    QPushButton *recalc_next_ = nullptr;
    QPushButton *recalc_all_ = nullptr;

    /// Категория поезда (аудит 3.8) и статус «изменения не применены» (аудит 3.4)
    QComboBox *class_box_ = nullptr;
    QLabel *state_label_ = nullptr;

    /// Режим таблицы (аудит 3.5) и признак неприменённых изменений (аудит 3.4)
    bool simple_mode_ = false;
    bool dirty_ = false;

    /// Номер станции плана для строки таблицы (-1 начало, -2 конец)
    QVector<int> row_station_;

    void rebuildPlan();
    void fillTable();
    void readTable();
    void showResult();
    void markEdited();
    void updateRouteReport();
    TripOptions options() const;

    /// Отметить, что изменения ещё не применены
    void markDirty();

    /// Показать/скрыть строку «изменения ещё не применены»
    void updateDirtyLabel();

    /// Разбор времени, вписанного в ячейку прибытия/отправления
    void parseManualTime(QTableWidgetItem *item);

    /// Вернуть строку к авторасчёту; true - были ручные времена
    bool clearManualRow(int row, int station);

    /// Список закреплённых станций и подпись кнопки «Закрепить/Убрать»
    void refreshPinControls();

    /// Индекс станции в state_.pins (-1 - не закреплена)
    int pinIndex(const QString &station) const;
};

} // namespace rse

#endif // RSE_TIMETABLE_DIALOG_H
