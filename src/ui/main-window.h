#ifndef     RSE_MAIN_WINDOW_H
#define     RSE_MAIN_WINDOW_H

#include    "scenario/export.h"
#include    "ui/event-dialog.h"
#include    "ui/timetable-dialog.h"
#include    <QMainWindow>

#include    "route/route.h"
#include    "catalog/vehicle-catalog.h"
#include    "scenario/scenario.h"

class QLabel;
class QAction;
class QDockWidget;

namespace rse
{

class MapWidget;
class RollingStockPanel;
class TrainsPanel;
class EventsPanel;

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

public slots:

    /// Щелчок по пути в режиме расстановки: диалог подтверждения и добавление поезда
    void placeAt(const rse::Route::Hit &hit);

    /// Развернуть поезд на месте (голова переходит на место хвоста)
    void reverseTrain(int index);

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

    /// Открыть проект: загружает его маршрут (и каталог игры) и расстановку
    bool openProject(const QString &path);

    /// Импорт существующего сценария (каталог с main.lua). interactive - спрашивать о замене
    /// текущей расстановки и показывать итог окном. false - тихо, для тестов
    bool importScenarioDir(const QString &dir, bool interactive = true, QStringList *report = nullptr);

    /// Экспорт сценария в каталог маршрута (диалог)
    void exportScenario();

    /// Экспорт без диалога (для тестов и сценариев). false - не записано, причина в error
    bool exportScenarioTo(const ExportOptions &options, QString *error = nullptr);

    const ProjectInfo &projectInfo() const
    {
        return project_info_;
    }

    const QString &projectPath() const
    {
        return project_path_;
    }

    /// Подсветить на карте путь выбранного поезда по расписанию
    void updateTripOverlay();

private slots:

    void onOpenRoute();
    void onOpenGame();
    void onShowWarnings();
    void onHoverChanged(const rse::Route::Hit &hit);
    void onManualModuleChanged();

private:

    Route route_;
    VehicleCatalog catalog_;
    Scenario scenario_;

    /// Состав, выбранный в панели для расстановки
    PlacedTrain template_;
    bool has_template_ = false;

    ProjectInfo project_info_;
    QString project_path_;

    EventsPanel *events_panel_ = nullptr;
    QDockWidget *events_dock_ = nullptr;
    bool event_pick_ = false;
    int event_index_ = -1;
    int event_select_ = -1;
    EventField event_field_ = EventField::None;
    ScenarioEvent event_state_;

    int pick_index_ = -1;
    TimetableState pick_state_;

    MapWidget *map_ = nullptr;
    RollingStockPanel *stock_panel_ = nullptr;
    QDockWidget *stock_dock_ = nullptr;
    TrainsPanel *trains_panel_ = nullptr;
    QDockWidget *trains_dock_ = nullptr;
    QLabel *hover_label_ = nullptr;
    QLabel *info_label_ = nullptr;
    QAction *warnings_action_ = nullptr;

    void createActions();
    void updateInfo();

    /// Файл с ручными привязками каталогов конфигов к модулям
    static QString manualModulesPath();

    /// Все замечания маршрута и каталога одним списком
    QStringList allWarnings() const;
};

} // namespace rse

#endif // RSE_MAIN_WINDOW_H
