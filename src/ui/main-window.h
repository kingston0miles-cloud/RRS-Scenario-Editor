#ifndef     RSE_MAIN_WINDOW_H
#define     RSE_MAIN_WINDOW_H

#include    "scenario/checker.h"
#include    "scenario/export.h"
#include    "ui/event-dialog.h"
#include    "ui/timetable-dialog.h"
#include    "ui/auto-events-panel.h"
#include    "ui/workflow-bar.h"
#include    <QMainWindow>
#include    <QPointer>

#include    "route/route.h"
#include    "catalog/vehicle-catalog.h"
#include    "scenario/scenario.h"

class QLabel;
class QPushButton;
class QAction;
class QDockWidget;

namespace rse
{

class MapWidget;
class RollingStockPanel;
class TrainsPanel;
class EventsPanel;
class CheckDialog;

//------------------------------------------------------------------------------
// Главное окно редактора сценариев
//------------------------------------------------------------------------------
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:

    explicit MainWindow(QWidget *parent = nullptr);

    /// Открыть маршрут из каталога. При ошибке показывает сообщение.
    /// Каталог игры для подвижного состава определяется по маршруту
    bool openRoute(const QString &route_dir);

    /// Загрузить подвижной состав из корня игры (cfg/vehicles, cfg/trains).
    /// При ошибке показывает сообщение, если show_errors
    bool openGame(const QString &game_root, bool show_errors = true);

    const VehicleCatalog &catalog() const
    {
        return catalog_;
    }

    RollingStockPanel *rollingStockPanel() const
    {
        return stock_panel_;
    }

    MapWidget *map() const
    {
        return map_;
    }

    TrainsPanel *trainsPanel() const
    {
        return trains_panel_;
    }

    const Route &route() const
    {
        return route_;
    }

    const Scenario &scenario() const
    {
        return scenario_;
    }

protected:

    void closeEvent(QCloseEvent *event) override;

public slots:

    /// Щелчок по пути в режиме расстановки: диалог подтверждения и добавление поезда
    void placeAt(const rse::Route::Hit &hit);

    /// Развернуть поезд на месте (голова переходит на место хвоста)
    void reverseTrain(int index);

    /// Перенести поезд головой в (траектория, координата) с направлением dir (перетаскивание
    /// мышью). false - нельзя (пересечение, тупик), поезд остаётся на месте
    bool moveTrain(int index, const QString &trajectory, double coord, int dir);

    /// Изменить положение, имя и флаг бота поезда в диалоге
    void editTrain(int index);

    /// Создать состав для cfg/trains: имя existing - правка готового (as_copy - как копия),
    /// пусто - новый, при заданной единице (config_dir, config) - с неё
    void composeTrain(const QString &existing, bool as_copy,
                      const QString &config_dir = QString(), const QString &config = QString());

    /// Записать состав в cfg/trains и перечитать каталог (без диалогов, для тестов
    /// и сценариев). overwrite - заменить существующий файл
    bool saveComposedTrain(const TrainConfigData &train, bool overwrite, QString *error = nullptr);

    /// Правка состава в диалоге: единицы, загрузка, разбивка на несколько поездов
    void editConsist(int index);

    /// Применить результат правки состава (без диалога): первая часть заменяет
    /// поезд index, остальные добавляются. false - правка не применена
    bool applyConsistEdit(int index, const PlacedTrain &edited, const QVector<int> &cuts);

    /// Удалить поезд из сценария
    void removeTrain(int index);

    /// Создать или изменить продолжение: локомотив index приходит к вагонам и сцепляется
    void editContinuation(int index);

    /// Пересчитать положение и состав продолжений после изменений локомотива и вагонов
    void syncContinuations();

    /// Окно проверки графиков (создаётся при первом показе)
    void showScheduleCheck();

    /// Результат проверки графиков для текущего сценария
    QVector<ScheduleIssue> checkResult() const;

    /// Обновить список поездов, предварительно пересчитав продолжения
    void refreshTrains();
    void refreshAutoEvents();

    /// График поезда в диалоге. resume - состояние диалога после выбора точки на карте
    void editTimetable(int index, const TimetableState *resume = nullptr);

    /// Применить рассчитанное расписание к поезду (nullptr - убрать)
    void setTimetable(int index, const Timetable &tt);

    /// Идёт выбор конечной точки графика на карте
    bool picking() const
    {
        return pick_index_ >= 0 || event_pick_;
    }

    /// События сценария: новое, правка в диалоге, удаление, перемещение
    void addEvent();
    void editEvent(int index, const ScenarioEvent *resume = nullptr, int select = -1);
    void removeEvent(int index);
    void moveEvent(int index, int delta);

    EventsPanel *eventsPanel() const
    {
        return events_panel_;
    }

    /// Время начала игры для проверок событий
    int effectiveStartTime() const;

    /// Закончить выбор точки на карте (hit.valid == false - отмена) и вернуться в диалог
    void finishPick(const Route::Hit &hit);

    /// Сохранить проект (расстановка, расписания, описание). Без пути спрашивает файл
    bool saveProjectAs(const QString &path);
    bool saveProject();

    /// Закрыть сценарий: подтверждение несохранённых изменений (как при выходе)
    /// и возврат программы к стартовому состоянию «сценарий не открыт»
    void closeScenario();

    /// Открыть проект: загружает его маршрут (и каталог игры) и расстановку
    bool openProject(const QString &path);

    /// Импорт существующего сценария (каталог с main.lua). interactive - спрашивать о замене
    /// текущей расстановки и показывать итог окном. false - тихо, для тестов
    bool importScenarioDir(const QString &dir, bool interactive = true, QStringList *report = nullptr);

    /// Экспорт сценария в каталог маршрута (диалог)
    void exportScenario();

    /// Экспорт без диалога (для тестов и сценариев). false - не записано, причина в error.
    /// План экспорта (файлы, резервная копия main.lua) возвращается в plan, если задан
    bool exportScenarioTo(const ExportOptions &options, QString *error = nullptr,
                          ExportPlan *plan = nullptr);

    const ProjectInfo &projectInfo() const
    {
        return project_info_;
    }

    const QString &projectPath() const
    {
        return project_path_;
    }

    /// Есть несохранённые изменения (расстановка, расписания, события, описание)
    bool isModified() const;

    /// Считать текущее состояние сохранённым
    void markClean();

    /// Панель шагов «маршрут → составы → … → экспорт» (аудит 1.3)
    WorkflowBar *workflowBar() const
    {
        return workflow_;
    }

    /// Список сочетаний клавиш для справки по F1 (аудит 1.5)
    QStringList shortcutHints() const;

    /// Разбивка замечаний загрузки по источникам (аудит 1.6)
    QString warningsBreakdown() const;

    /// Каталоги маршрутов игры, пригодные для открытия (аудит 4.6)
    static QStringList scanRoutes(const QString &game_root);

    /// Стартовый экран: выбор папки игры и маршрута (аудит 4.6)
    void maybeShowStartScreen();
    void showStartScreen();

    /// Запомнить/восстановить размеры и видимость панелей (аудит 1.7)
    void saveWindowState();
    void restoreWindowState();

    /// Отмена и повтор последних действий (этап 24, снимки projectSnapshot)
    void undo();
    void redo();
    bool canUndo() const
    {
        return !undo_stack_.isEmpty();
    }

    bool canRedo() const
    {
        return !redo_stack_.isEmpty();
    }

    /// Подсветить на карте путь выбранного поезда по расписанию
    void updateTripOverlay();

private slots:

    void onOpenRoute();
    void onOpenGame();
    void onShowWarnings();
    void onShowShortcuts();
    void onHoverChanged(const rse::Route::Hit &hit);
    void onManualModuleChanged();
    void onWorkflowStep(int step);
    void onAssignModuleRequested(const QString &config_dir);

private:

    Route route_;
    VehicleCatalog catalog_;
    Scenario scenario_;

    /// Состав, выбранный в панели для расстановки
    PlacedTrain template_;
    bool has_template_ = false;

    QByteArray clean_snapshot_;
    ProjectInfo project_info_;
    QString project_path_;

    /// Отмена/повтор: снимки проекта до каждого изменения (этап 24)
    QVector<QByteArray> undo_stack_;
    QVector<QByteArray> redo_stack_;
    int undo_limit_ = 40;
    QAction *undo_action_ = nullptr;
    QAction *redo_action_ = nullptr;

    EventsPanel *events_panel_ = nullptr;
    AutoEventsPanel *auto_events_panel_ = nullptr;
    QDockWidget *auto_events_dock_ = nullptr;
    QDockWidget *events_dock_ = nullptr;
    bool event_pick_ = false;
    int event_index_ = -1;
    int event_select_ = -1;
    EventField event_field_ = EventField::None;
    ScenarioEvent event_state_;

    int pick_index_ = -1;
    int timetable_index_ = -1;
    QPointer<TimetableDialog> timetable_dialog_;

    MapWidget *map_ = nullptr;
    CheckDialog *check_dialog_ = nullptr;
    RollingStockPanel *stock_panel_ = nullptr;
    QDockWidget *stock_dock_ = nullptr;
    TrainsPanel *trains_panel_ = nullptr;
    QDockWidget *trains_dock_ = nullptr;
    WorkflowBar *workflow_ = nullptr;
    QLabel *hover_label_ = nullptr;
    QPushButton *warnings_button_ = nullptr;
    QLabel *info_label_ = nullptr;
    QAction *warnings_action_ = nullptr;

    void createActions();
    void updateInfo();
    void updateWorkflowState();

    /// Снимок текущего состояния в стек отмены (перед изменением); повтор очищается
    void pushUndo();

    /// Очистить историю (новая база: открытие маршрута/проекта, закрытие, импорт)
    void clearUndo();

    /// Восстановить состояние из снимка
    void restoreSnapshot(const QByteArray &snapshot);

    void updateUndoActions();

    /// Подтверждение «Сохранить / Не сохранять / Отмена» при несохранённых
    /// изменениях (выход и закрытие сценария). false - действие отменяется
    bool confirmUnsavedChanges();

    /// Сброс к стартовому состоянию «сценарий не открыт»: без маршрута и игры,
    /// пустая расстановка, панели и карта очищены, заголовок без имени
    void resetToStartup();

    /// Файл с ручными привязками каталогов конфигов к модулям
    static QString manualModulesPath();

    /// Все замечания маршрута и каталога одним списком
    QStringList allWarnings() const;
};

} // namespace rse

#endif // RSE_MAIN_WINDOW_H
