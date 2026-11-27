#ifndef     RSE_TIMETABLE_DIALOG_H
#define     RSE_TIMETABLE_DIALOG_H

#include    <QDialog>
#include    <QHash>
#include    <QSet>

#include    "catalog/vehicle-catalog.h"
#include    "scenario/stationroute.h"
#include    "scenario/trip.h"
#include    "ui/map-widget.h"

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QTableWidget;
class QTableWidgetItem;
class QTimeEdit;
class QTextBrowser;
class QToolButton;

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

    /// Платформы станции (этап 27): -1 не задано, 0 нет, 1 есть; переносятся
    /// в расписание как есть
    int rightPlatform = -1;
    int leftPlatform = -1;

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

    /// Станции, убранные из графика красным крестом: строк в таблице не будет,
    /// пока станцию не добавят снова плюсом
    QSet<QString> removedStations;

    /// Станции, добавленные в график плюсом между строками и не лежащие на пути
    /// плана (по порядку таблицы)
    QStringList addedStations;

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

    /// Маршрут изменился: перерисовать на карте (конфликтные пути - для красной
    /// разметки; по умолчанию пусто)
    void tripChanged(const QVector<PathSegment> &path, const QVector<TimetableStation> &stations,
                     const QStringList &conflicts = {});

    /// Светофоры, которые откроются маршрутами графика: подсветить на карте
    /// (пусто - убрать подсветку)
    void signalMarksChanged(const QVector<SignalMark> &marks);

    /// Пользователь щёлкнул по пути в замечании: показать путь на карте
    void locateRequested(const QString &trajectory);

public:

    /// Коды завершения, кроме Accepted/Rejected
    enum Result
    {
        PickRequested = 2,
        RemoveRequested = 3
    };

    TimetableDialog(const Route &route, const VehicleCatalog &catalog, const Scenario &scenario,
                    int train_index, const TimetableState &state, QWidget *parent = nullptr);

    ~TimetableDialog();

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

    /// Номер поезда (имя файла расписания) из окна; перенесён из «Положения»
    QString trainName() const;

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
        ColPlatform,
        ColRemove,
        ColCount
    };

public slots:

    /// Задать конечную точку
    void setEnd(const QString &trajectory, double coord);

    /// Вставить строку новой станции между строками (row - индекс, перед которым
    /// вставлять; вызывается зелёным «+» на границе строк, доступно проверкам)
    void insertRowAt(int row);

private slots:

    void onWaypointChosen(int index);
    void onDefaultReserve();
    void recompute();
    void onTrackChosen();
    void onPinButton();
    void onPinSelected();
    void onRecalcNext();
    void onRecalcAll();

    /// Убрать станцию из графика красным крестом (столбец «Удалить»)
    void onRemoveRow();

    /// В добавленной строке выбрана станция из выпадающего списка
    void onAddedStationChosen();

private:

    /// Данные добавленной строки: имя станции, положение на пути и отступ от него
    struct AddedRowInfo
    {
        QString name;
        double s = 0.0;
        double offset = 0.0;
    };

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

    /// Ошибка перестроения пути, из-за которой график оставлен прежним (правка:
    /// график не сбрасывается, строка-виновник подсвечивается красным)
    QString plan_error_;

    /// Станция-виновник ошибки пути (пусто - виновата конечная точка или ни одна)
    QString problem_station_;

    /// Виновата конечная точка маршрута
    bool problem_is_end_ = false;

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

    /// Зелёный «+» на границе строк: вставить станцию между соседними
    QToolButton *insert_btn_ = nullptr;
    int insert_row_ = -1;

    /// Замечания расчёта; пути в тексте кликабельны (QTextBrowser)
    QTextBrowser *warnings_ = nullptr;
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

    /// Номер поезда (имя файла расписания), перенесён из «Положения»
    QLineEdit *name_edit_ = nullptr;

    /// Режим таблицы (аудит 3.5) и признак неприменённых изменений (аудит 3.4)
    bool simple_mode_ = false;
    bool dirty_ = false;

    /// Номер станции плана для строки таблицы (-1 начало, -2 конец,
    /// -10-k добавленная строка)
    QVector<int> row_station_;

    /// Данные добавленных строк таблицы, по порядку создания
    QVector<AddedRowInfo> row_added_;

    void rebuildPlan();
    void fillTable();

    /// Добавить строку таблицы: ячейки, комбобоксы станции и виджеты режима;
    /// added - строка с выпадающим списком станции (вставленная плюсом);
    /// at - индекс вставки в середину таблицы (-1 - в конец); возвращает строку
    int addTableRow(int station, const QString &name, double s, bool fixed,
                    bool added = false, int at = -1);

    void readTable();
    void showResult();

    /// Загрузить настройки станции из состояния в виджеты строки (галочка,
    /// режим, путь, стоянка, запас, платформы)
    void applyRowSettings(int r, const QString &name);

    /// Показать ошибку расчёта: очистить расчётные колонки таблицы
    void showFailure();

    /// Путь перестроить не удалось, график оставлен прежним: предупреждение
    /// и подсветка станции-виновника лёгким красным
    void showPlanProblem();

    /// Подсветить строку r как конфликтную (problem) или вернуть обычный вид
    void highlightRow(int r, bool problem);

    /// Имя станции в строке (текст ячейки «Станция»)
    QString rowNameAt(int row) const;

    /// Категория строки: 0 начало, 1 конец, 2 станция плана, 3 добавленная
    int rowKindAt(int row) const;

    /// Строка, в которой сидит виджет ячейки col (-1 - не найден)
    int rowOfWidget(QWidget *w, int col) const;

    /// Ближайшая к станции точка пути: s и отступ станции от пути. false -
    /// станция не найдена в маршруте
    bool nearestPathS(const QString &station, double *s, double *offset) const;

    /// Заполнить комбобокс путей станции (выбранный путь - selected)
    void fillTrackCombo(QComboBox *track, const QString &station, const QString &selected);

    /// Вернуть прокрутку таблицы после перестроения: на прежнюю строку,
    /// если она есть, иначе на прежнюю позицию
    void restoreScroll(int saved, const QString &name, int kind);

    /// Показать зелёный «+» на границе строк (row - индекс строки, перед которой
    /// вставлять); row <= 0 - скрыть
    void showInsertButton(int row);

    /// Ячейка «Маршрут» и подсветка светофоров для строки k результата
    void showRouteCell(int r, int k, const QColor &normalColor, const QColor &badColor,
                       QVector<SignalMark> *marks);

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
