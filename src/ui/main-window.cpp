#include    "ui/check-dialog.h"
#include    "ui/continuation-dialog.h"
#include    "scenario/formation.h"
#include    "ui/main-window.h"

#include    "ui/consist-editor-dialog.h"
#include    "scenario/importer.h"
#include    "ui/event-dialog.h"
#include    "ui/events-panel.h"
#include    "ui/export-dialog.h"
#include    <QFileInfo>
#include    "ui/map-widget.h"
#include    "ui/place-train-dialog.h"
#include    "ui/trains-panel.h"
#include    "ui/rolling-stock-panel.h"

#include    <QAction>
#include    <QCheckBox>
#include    <QCloseEvent>
#include    <QCryptographicHash>
#include    <QDateTime>
#include    <QDialog>
#include    <QLineEdit>
#include    <QListWidget>
#include    <QPushButton>
#include    <QDir>
#include    <QDockWidget>
#include    <QFile>
#include    <QFileDialog>
#include    <QLabel>
#include    <QMenuBar>
#include    <QMessageBox>
#include    <QSettings>
#include    <QTimer>
#include    <QToolButton>
#include    <QTreeWidget>
#include    <QHeaderView>
#include    <QVBoxLayout>
#include    "scenario/autotriggers.h"
#include    "scenario/pathfinder.h"
#include    <QStandardPaths>
#include    <QStatusBar>

namespace rse
{

namespace
{

const char *SETTINGS_ORG = "rrs-scenario-editor";
const char *SETTINGS_APP = "rrs-scenario-editor";
const char *KEY_LAST_ROUTE_DIR = "lastRouteDir";
const char *KEY_LAST_GAME_DIR = "lastGameDir";

} // namespace

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(tr("Редактор сценариев RRS"));
    resize(1280, 760);

    // Панель шагов «маршрут → составы → … → экспорт» над картой (аудит 1.3)
    auto *central = new QWidget(this);
    auto *central_layout = new QVBoxLayout(central);
    central_layout->setContentsMargins(0, 0, 0, 0);
    central_layout->setSpacing(0);

    workflow_ = new WorkflowBar(central);
    central_layout->addWidget(workflow_);

    map_ = new MapWidget(central);
    central_layout->addWidget(map_, 1);

    setCentralWidget(central);

    connect(workflow_, &WorkflowBar::stepActivated, this, &MainWindow::onWorkflowStep);

    stock_panel_ = new RollingStockPanel(this);
    stock_panel_->setCatalog(&catalog_);

    stock_dock_ = new QDockWidget(tr("Подвижной состав"), this);
    stock_dock_->setObjectName(QStringLiteral("rollingStockDock"));
    stock_dock_->setWidget(stock_panel_);
    stock_dock_->setMinimumWidth(360);
    addDockWidget(Qt::RightDockWidgetArea, stock_dock_);

    // Начальная ширина: чтобы колонка «Тип / модуль» помещалась без прокрутки
    resizeDocks({ stock_dock_ }, { 560 }, Qt::Horizontal);

    trains_panel_ = new TrainsPanel(this);
    trains_panel_->setScenario(&scenario_, &route_);

    trains_dock_ = new QDockWidget(tr("Поезда сценария"), this);
    trains_dock_->setObjectName(QStringLiteral("trainsDock"));
    trains_dock_->setWidget(trains_panel_);
    addDockWidget(Qt::BottomDockWidgetArea, trains_dock_);
    resizeDocks({ trains_dock_ }, { 170 }, Qt::Vertical);

    events_panel_ = new EventsPanel(this);
    events_panel_->setEvents(&project_info_.events);

    events_dock_ = new QDockWidget(tr("События сценария"), this);
    events_dock_->setObjectName(QStringLiteral("eventsDock"));
    events_dock_->setWidget(events_panel_);
    addDockWidget(Qt::BottomDockWidgetArea, events_dock_);
    tabifyDockWidget(trains_dock_, events_dock_);

    auto_events_panel_ = new AutoEventsPanel(this);
    auto_events_dock_ = new QDockWidget(tr("События по графикам (авто)"), this);
    auto_events_dock_->setObjectName(QStringLiteral("autoEventsDock"));
    auto_events_dock_->setWidget(auto_events_panel_);
    addDockWidget(Qt::BottomDockWidgetArea, auto_events_dock_);
    tabifyDockWidget(events_dock_, auto_events_dock_);
    trains_dock_->raise();

    connect(auto_events_panel_, &AutoEventsPanel::routeSelected, this,
            [this](const QString &from, const QString &to, int dir)
            {
                const GameRoute g = gameRoute(route_, from, to, dir);
                QVector<PathSegment> segs;

                for (const GameRouteStep &st : g.steps)
                {
                    const TrajectoryData *t = route_.find(st.trajectory);
                    const double len = (t != nullptr) ? t->length : 0.0;
                    PathSegment ps;
                    ps.trajectory = st.trajectory;
                    ps.from = (st.dir > 0) ? 0.0 : len;
                    ps.to = (st.dir > 0) ? len : 0.0;
                    ps.dir = st.dir;
                    segs.push_back(ps);
                }

                map_->setTripOverlay(segs, {});
            });
    connect(auto_events_panel_, &AutoEventsPanel::selectionCleared, this, [this] { updateTripOverlay(); });

    // Флажок «не создавать» в панели: события маршрутов по графикам для поезда
    // не создаются вовсе (то же, что и в положении поезда)
    connect(auto_events_panel_, &AutoEventsPanel::noAutoEventsChanged, this,
            [this](const QString &train, bool off)
            {
                const int index = scenario_.indexOfName(train);

                if (index < 0)
                {
                    return;
                }

                PlacedTrain t = scenario_.trains()[index];
                t.noAutoEvents = off;

                pushUndo();

                scenario_.replace(index, t);
                refreshTrains();
            });
    connect(auto_events_dock_, &QDockWidget::visibilityChanged, this, [this](bool on)
    {
        if (on)
        {
            refreshAutoEvents();
        }
    });

    map_->setScenario(&scenario_);

    hover_label_ = new QLabel(this);
    info_label_ = new QLabel(this);

    statusBar()->addWidget(hover_label_, 1);
    statusBar()->addPermanentWidget(info_label_);
    warnings_button_ = new QPushButton(this);
    warnings_button_->setFlat(true);
    warnings_button_->setCursor(Qt::PointingHandCursor);
    warnings_button_->setToolTip(tr("Показать список замечаний"));
    warnings_button_->hide();
    statusBar()->addPermanentWidget(warnings_button_);

    // Ручные привязки модулей переживают перезапуск программы и смену маршрута
    catalog_.loadManualModules(manualModulesPath());

    createActions();
    restoreWindowState();
    markClean();
    updateInfo();

    // Стартовый экран (аудит 4.6) - когда очередь событий заработает и маршрут
    // к этому моменту не открыт (в тестах маршрут открывают до этого)
    QTimer::singleShot(0, this, [this] { maybeShowStartScreen(); });

    connect(map_, &MapWidget::hoverChanged, this, &MainWindow::onHoverChanged);

    connect(map_, &MapWidget::trajectoryCopied, this, [this](const QString &text)
    {
        statusBar()->showMessage(tr("Имя траектории скопировано: %1").arg(text), 4000);
    });

    connect(stock_panel_, &RollingStockPanel::manualModuleChanged,
            this, &MainWindow::onManualModuleChanged);

    // Выбрали состав в панели - карта переходит в режим расстановки
    connect(stock_panel_, &RollingStockPanel::consistSelected, this,
            [this](const PlacedTrain &train)
    {
        template_ = train;
        has_template_ = true;
        map_->setPlacementTemplate(&template_);

        statusBar()->showMessage(
            tr("Выбран «%1» (%2 м). Кликните по пути на карте. Esc — отмена выбора.")
                .arg(train.title).arg(train.length, 0, 'f', 1), 8000);
    });

    connect(stock_panel_, &RollingStockPanel::newTrainRequested, this,
            [this](const QString &dir, const QString &cfg) { composeTrain(QString(), false, dir, cfg); });

    connect(stock_panel_, &RollingStockPanel::editTrainRequested, this,
            [this](const QString &name, bool copy) { composeTrain(name, copy); });

    connect(stock_panel_, &RollingStockPanel::consistCleared, this, [this]
    {
        has_template_ = false;
        map_->setPlacementTemplate(nullptr);
    });

    connect(map_, &MapWidget::trackClicked, this, &MainWindow::placeAt);

    connect(map_, &MapWidget::trainDropped, this,
            [this](int index, const QString &traj, double coord, int dir) { moveTrain(index, traj, coord, dir); });
    connect(map_, &MapWidget::trainClicked, this, [this](int index) { trains_panel_->setCurrentIndex(index); });

    connect(trains_panel_, &TrainsPanel::removeRequested, this, &MainWindow::removeTrain);
    connect(trains_panel_, &TrainsPanel::reverseRequested, this, &MainWindow::reverseTrain);
    connect(trains_panel_, &TrainsPanel::editRequested, this, &MainWindow::editTrain);
    connect(trains_panel_, &TrainsPanel::consistRequested, this, &MainWindow::editConsist);
    connect(trains_panel_, &TrainsPanel::timetableRequested, this,
            [this](int index) { editTimetable(index); });
    connect(trains_panel_, &TrainsPanel::continuationRequested, this, [this](int index) { editContinuation(index); });
    connect(events_panel_, &EventsPanel::addRequested, this, &MainWindow::addEvent);
    connect(events_panel_, &EventsPanel::editRequested, this, [this](int i) { editEvent(i); });
    connect(events_panel_, &EventsPanel::removeRequested, this, &MainWindow::removeEvent);
    connect(events_panel_, &EventsPanel::moveRequested, this, &MainWindow::moveEvent);
    connect(trains_panel_, &TrainsPanel::selectionChanged, this, [this] { updateTripOverlay(); });
    connect(trains_panel_, &TrainsPanel::selectionChanged, map_, &MapWidget::setSelectedTrain);

    connect(trains_panel_, &TrainsPanel::showRequested, this, [this](int index)
    {
        if (index >= 0 && index < scenario_.size())
        {
            const PlacedTrain &t = scenario_.trains()[index];
            map_->centerOn(t.trajectory, t.coord);
        }
    });
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
QString MainWindow::manualModulesPath()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);

    return dir + QStringLiteral("/vehicle-modules.json");
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void MainWindow::createActions()
{
    QMenu *file_menu = menuBar()->addMenu(tr("&Файл"));

    QAction *open = file_menu->addAction(tr("&Открыть маршрут…"));
    open->setShortcut(QKeySequence::Open);
    connect(open, &QAction::triggered, this, &MainWindow::onOpenRoute);

    QAction *open_game = file_menu->addAction(tr("&Указать каталог игры…"));
    connect(open_game, &QAction::triggered, this, &MainWindow::onOpenGame);

    QAction *new_train = file_menu->addAction(tr("&Новый состав…"));
    new_train->setShortcut(QKeySequence::New);
    connect(new_train, &QAction::triggered, this, [this] { composeTrain(QString(), false); });

    file_menu->addSeparator();

    QAction *open_project = file_menu->addAction(tr("Открыть &проект…"));
    open_project->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_O));
    connect(open_project, &QAction::triggered, this, [this]
    {
        QSettings settings(SETTINGS_ORG, SETTINGS_APP);
        const QString path = QFileDialog::getOpenFileName(
            this, tr("Открыть проект"), settings.value("lastProjectDir", QDir::homePath()).toString(),
            tr("Проекты сценариев (*.rrsproj);;Все файлы (*)"));

        if (!path.isEmpty())
        {
            openProject(path);
        }
    });

    QAction *import_action = file_menu->addAction(tr("&Импортировать сценарий…"));
    connect(import_action, &QAction::triggered, this, [this]
    {
        if (!route_.isLoaded())
        {
            QMessageBox::information(this, tr("Импорт"), tr("Сначала откройте маршрут."));
            return;
        }

        const QString start = QDir(route_.dir()).filePath(QStringLiteral("scenarios"));
        const QString dir = QFileDialog::getExistingDirectory(
            this, tr("Каталог сценария (с файлом main.lua)"), QDir(start).exists() ? start : route_.dir());

        if (!dir.isEmpty())
        {
            importScenarioDir(dir);
        }
    });

    QAction *save_project = file_menu->addAction(tr("&Сохранить проект"));
    save_project->setShortcut(QKeySequence::Save);
    connect(save_project, &QAction::triggered, this, [this] { saveProject(); });

    QAction *save_as = file_menu->addAction(tr("Сохранить проект &как…"));
    save_as->setShortcut(QKeySequence::SaveAs);
    connect(save_as, &QAction::triggered, this, [this] { saveProjectAs(QString()); });

    QAction *export_action = file_menu->addAction(tr("&Экспорт сценария…"));
    export_action->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_E));
    connect(export_action, &QAction::triggered, this, &MainWindow::exportScenario);

    QAction *close_scenario = file_menu->addAction(tr("&Закрыть сценарий"));
    close_scenario->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_W));
    close_scenario->setToolTip(tr("Закрыть сценарий и вернуться к стартовому состоянию "
                                  "(новый сценарий без перезапуска программы)"));
    connect(close_scenario, &QAction::triggered, this, &MainWindow::closeScenario);

    file_menu->addSeparator();

    QAction *quit = file_menu->addAction(tr("В&ыход"));
    quit->setShortcut(QKeySequence::Quit);
    connect(quit, &QAction::triggered, this, &QWidget::close);

    QMenu *edit_menu = menuBar()->addMenu(tr("&Правка"));

    undo_action_ = edit_menu->addAction(tr("&Отменить"));
    undo_action_->setShortcut(QKeySequence::Undo);
    undo_action_->setEnabled(false);
    connect(undo_action_, &QAction::triggered, this, &MainWindow::undo);

    redo_action_ = edit_menu->addAction(tr("&Повторить"));
    redo_action_->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z));
    redo_action_->setEnabled(false);
    connect(redo_action_, &QAction::triggered, this, &MainWindow::redo);

    QMenu *help_menu = menuBar()->addMenu(tr("&Справка"));

    QAction *shortcuts = help_menu->addAction(tr("Сочетания клавиш"));
    shortcuts->setShortcut(Qt::Key_F1);
    shortcuts->setToolTip(tr("Список клавиш редактора и карты (аудит 1.5)"));
    connect(shortcuts, &QAction::triggered, this, &MainWindow::onShowShortcuts);

    QMenu *check_menu = menuBar()->addMenu(tr("&Проверка"));

    QAction *check = check_menu->addAction(tr("Проверить &графики сценария…"));
    check->setShortcut(Qt::Key_F7);
    check->setToolTip(tr("Пересечения маршрутов, встречное движение, заторы и ошибки в расписаниях"));
    connect(check, &QAction::triggered, this, &MainWindow::showScheduleCheck);

    QMenu *view_menu = menuBar()->addMenu(tr("&Вид"));

    QAction *fit = view_menu->addAction(tr("Показать весь &маршрут"));
    fit->setShortcut(Qt::Key_F);
    connect(fit, &QAction::triggered, map_, &MapWidget::fitAll);

    QAction *rotate = view_menu->addAction(tr("&Повернуть на 90°"));
    rotate->setShortcut(Qt::Key_R);
    connect(rotate, &QAction::triggered, map_, &MapWidget::rotateView);

    view_menu->addSeparator();

    QAction *names = view_menu->addAction(tr("Имена &траекторий"));
    names->setCheckable(true);
    names->setChecked(map_->showNames());
    connect(names, &QAction::toggled, map_, &MapWidget::setShowNames);

    QAction *arrows = view_menu->addAction(tr("&Направление путей (стрелки)"));
    arrows->setCheckable(true);
    arrows->setChecked(map_->showArrows());
    connect(arrows, &QAction::toggled, map_, &MapWidget::setShowArrows);

    QAction *switches = view_menu->addAction(tr("&Стрелочные переводы"));
    switches->setCheckable(true);
    switches->setChecked(map_->showSwitches());
    connect(switches, &QAction::toggled, map_, &MapWidget::setShowSwitches);

    view_menu->addSeparator();

    // Показать или скрыть панель подвижного состава
    view_menu->addAction(stock_dock_->toggleViewAction());
    view_menu->addAction(trains_dock_->toggleViewAction());
    view_menu->addAction(events_dock_->toggleViewAction());

    QAction *cancel = new QAction(tr("Отменить выбор состава"), this);
    cancel->setShortcut(Qt::Key_Escape);
    addAction(cancel);
    connect(cancel, &QAction::triggered, stock_panel_, &RollingStockPanel::clearSelection);
    connect(cancel, &QAction::triggered, this, [this] { finishPick(Route::Hit()); });

    view_menu->addSeparator();

    warnings_action_ = view_menu->addAction(tr("Замечания при загрузке…"));
    warnings_action_->setEnabled(false);
    connect(warnings_action_, &QAction::triggered, this, &MainWindow::onShowWarnings);
    connect(warnings_button_, &QPushButton::clicked, this, &MainWindow::onShowWarnings);
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void MainWindow::onOpenRoute()
{
    QSettings settings(SETTINGS_ORG, SETTINGS_APP);

    const QString start_dir = settings.value(KEY_LAST_ROUTE_DIR, QDir::homePath()).toString();

    const QString dir = QFileDialog::getExistingDirectory(
        this, tr("Каталог маршрута (routes/<имя маршрута>)"), start_dir);

    if (dir.isEmpty())
    {
        return;
    }

    openRoute(dir);
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void MainWindow::onOpenGame()
{
    QSettings settings(SETTINGS_ORG, SETTINGS_APP);

    QString start_dir = catalog_.gameRoot();

    if (start_dir.isEmpty())
    {
        start_dir = settings.value(KEY_LAST_GAME_DIR, QDir::homePath()).toString();
    }

    const QString dir = QFileDialog::getExistingDirectory(
        this, tr("Корневой каталог игры (рядом с папками cfg, routes, modules)"), start_dir);

    if (dir.isEmpty())
    {
        return;
    }

    openGame(dir);
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
bool MainWindow::openRoute(const QString &route_dir)
{
    Route loaded;

    if (!loaded.load(route_dir))
    {
        QMessageBox::critical(this, tr("Не удалось открыть маршрут"), loaded.errorString());
        return false;
    }

    route_ = std::move(loaded);

    // Положения поездов относятся к прежнему маршруту
    scenario_.clear();
    project_info_.events.clear();
    clearUndo();
    refreshTrains();
    events_panel_->refresh();
    markClean();

    map_->setRoute(&route_);

    QSettings settings(SETTINGS_ORG, SETTINGS_APP);
    settings.setValue(KEY_LAST_ROUTE_DIR, QDir(route_.dir()).absolutePath());

    setWindowTitle(tr("Редактор сценариев RRS — %1").arg(route_.title()));
    hover_label_->clear();

    // Подвижной состав берём из той же игры, где лежит маршрут (routes/<имя>).
    // Если маршрут лежит вне игры, пробуем каталог игры, указанный в прошлый раз
    const QString game_root = VehicleCatalog::gameRootForRoute(route_.dir());

    if (VehicleCatalog::isGameRoot(game_root))
    {
        openGame(game_root, false);
    }
    else
    {
        const QString last = settings.value(KEY_LAST_GAME_DIR).toString();

        if (!catalog_.isLoaded() && VehicleCatalog::isGameRoot(last))
        {
            openGame(last, false);
        }
        else if (!catalog_.isLoaded())
        {
            statusBar()->showMessage(
                tr("Рядом с маршрутом не найден каталог игры. Подвижной состав: "
                   "Файл → Указать каталог игры…"), 10000);
        }
    }

    updateInfo();

    return true;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
bool MainWindow::openGame(const QString &game_root, bool show_errors)
{
    if (!catalog_.load(game_root))
    {
        if (show_errors)
        {
            QMessageBox::critical(this, tr("Не удалось загрузить подвижной состав"),
                                  catalog_.errorString());
        }

        stock_panel_->refresh();
        updateInfo();

        return false;
    }

    QSettings settings(SETTINGS_ORG, SETTINGS_APP);
    settings.setValue(KEY_LAST_GAME_DIR, catalog_.gameRoot());

    stock_panel_->refresh();
    updateInfo();

    return true;
}

//------------------------------------------------------------------------------
// Щелчок по пути: подтверждение в диалоге, затем поезд попадает в сценарий
//------------------------------------------------------------------------------
void MainWindow::placeAt(const Route::Hit &hit)
{
    if (picking())
    {
        finishPick(hit);
        return;
    }

    if (!has_template_ || !hit.valid || !route_.isLoaded())
    {
        return;
    }

    PlacedTrain train = template_;
    train.name = scenario_.uniqueName(tr("Поезд"));
    train.trajectory = hit.trajectory;
    train.coord = hit.coord;

    // Голова по умолчанию смотрит по правильному направлению пути («туда» или
    // «обратно»); если оно неизвестно - вдоль траектории. «Развернуть» меняет это
    const int path_dir = route_.orientation(hit.trajectory);
    train.dir = (path_dir != 0) ? path_dir : 1;

    PlaceTrainDialog dialog(route_, scenario_, train, -1, this);
    connect(&dialog, &PlaceTrainDialog::assignModuleRequested, this, &MainWindow::onAssignModuleRequested);

    if (dialog.exec() != QDialog::Accepted)
    {
        return;
    }

    const int index = scenario_.add(dialog.result());

    pushUndo();

    refreshTrains();
    trains_panel_->setCurrentIndex(index);
    map_->update();

    statusBar()->showMessage(tr("Поезд «%1» поставлен на %2").arg(dialog.result().name,
                                                              dialog.result().trajectory), 5000);
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void MainWindow::reverseTrain(int index)
{
    if (index < 0 || index >= scenario_.size())
    {
        return;
    }

    const PlacedTrain t = Scenario::reversedInPlace(route_, scenario_.trains()[index]);
    const PlacementCheck chk = scenario_.check(route_, t, index);

    if (chk.hasProblems())
    {
        QMessageBox::warning(this, tr("Развернуть на месте"),
                             tr("Развернуть не получится: после разворота состав "
                                "пересекается с другим поездом или не умещается на путях."));
        return;
    }

    pushUndo();

    scenario_.replace(index, t);
    refreshTrains();
    map_->update();
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
bool MainWindow::moveTrain(int index, const QString &trajectory, double coord, int dir)
{
    if (index < 0 || index >= scenario_.size())
    {
        return false;
    }

    PlacedTrain t = scenario_.trains()[index];

    if (t.trajectory == trajectory && t.coord == coord && t.dir == dir)
    {
        return true;
    }

    t.trajectory = trajectory;
    t.coord = coord;
    t.dir = dir;
    t.timetable = Timetable();

    const PlacementCheck chk = scenario_.check(route_, t, index);

    if (chk.hasProblems())
    {
        statusBar()->showMessage(chk.overlap > 0.0
                                 ? tr("Здесь нельзя: состав пересекается с другим поездом")
                                 : tr("Здесь нельзя: состав не умещается на путях"), 5000);
        return false;
    }

    const bool had_timetable = scenario_.trains()[index].timetable.valid;

    pushUndo();

    scenario_.replace(index, t);
    refreshTrains();
    trains_panel_->setCurrentIndex(index);
    updateTripOverlay();
    map_->update();

    statusBar()->showMessage(had_timetable
                             ? tr("Поезд «%1» перенесён на %2; расписание сброшено — задайте его заново")
                               .arg(t.name, trajectory)
                             : tr("Поезд «%1» перенесён на %2").arg(t.name, trajectory), 6000);

    return true;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void MainWindow::editTrain(int index)
{
    if (index < 0 || index >= scenario_.size())
    {
        return;
    }

    PlaceTrainDialog dialog(route_, scenario_, scenario_.trains()[index], index, this);

    connect(&dialog, &PlaceTrainDialog::assignModuleRequested, this, &MainWindow::onAssignModuleRequested);

    if (dialog.exec() == QDialog::Accepted)
    {
        const PlacedTrain old = scenario_.trains()[index];
        PlacedTrain t = dialog.result();

        if (t.trajectory != old.trajectory || t.coord != old.coord || t.dir != old.dir)
        {
            t.timetable = Timetable();
        }

        pushUndo();

        scenario_.replace(index, t);
        refreshTrains();
        updateTripOverlay();
        map_->update();
    }
}

//------------------------------------------------------------------------------
// Создание и правка состава для cfg/trains
//------------------------------------------------------------------------------
void MainWindow::composeTrain(const QString &existing, bool as_copy,
                              const QString &config_dir, const QString &config)
{
    if (!catalog_.isLoaded())
    {
        QMessageBox::information(this, tr("Состав"),
                                 tr("Сначала загрузите подвижной состав: откройте маршрут из каталога игры "
                                    "или Файл → Указать каталог игры…"));
        return;
    }

    TrainConfigData initial;

    if (!existing.isEmpty())
    {
        const TrainConfigData *t = catalog_.findTrain(existing);

        if (t == nullptr)
        {
            return;
        }

        initial = *t;

        if (as_copy)
        {
            initial.name = existing + QStringLiteral("-copy");
            initial.title = tr("%1 (копия)").arg(t->title);
        }
    }
    else if (!config.isEmpty())
    {
        const VehicleConfigData *v = catalog_.findVehicle(config_dir, config);

        if (v != nullptr)
        {
            ConsistGroup g;
            g.module = v->module;
            g.configDir = v->configDir;
            g.config = v->config;
            g.count = 1;
            g.payloadCoeff = 1.0;
            initial.groups << g;
            initial.name = config;
            initial.title = config;
        }
    }

    ConsistEditorDialog dialog(catalog_, initial, this);

    while (dialog.exec() == QDialog::Accepted)
    {
        const TrainConfigData cfg = dialog.trainConfig();
        const bool replacing = QFileInfo::exists(catalog_.trainFilePath(cfg.name));

        if (replacing)
        {
            const bool same_file = (!existing.isEmpty() && !as_copy && cfg.name == existing);

            const auto answer = QMessageBox::question(
                this, tr("Файл уже есть"),
                same_file ? tr("Заменить состав «%1» в cfg/trains? Прежний файл будет перезаписан.").arg(cfg.name)
                          : tr("Состав с именем «%1» уже есть в cfg/trains. Заменить его?").arg(cfg.name));

            if (answer != QMessageBox::Yes)
            {
                continue;   // вернуться в диалог, чтобы выбрать другое имя
            }
        }

        QString error;

        if (!saveComposedTrain(cfg, replacing, &error))
        {
            QMessageBox::warning(this, tr("Не удалось сохранить состав"), error);
            continue;
        }

        stock_panel_->selectTrain(cfg.name);
        statusBar()->showMessage(tr("Состав «%1» сохранён: %2").arg(cfg.name, catalog_.trainFilePath(cfg.name)), 8000);
        break;
    }
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
bool MainWindow::saveComposedTrain(const TrainConfigData &train, bool overwrite, QString *error)
{
    if (!catalog_.saveTrain(train, overwrite, error))
    {
        return false;
    }

    // Перечитываем каталог: состав появляется в списке, модули пересчитываются
    const QString root = catalog_.gameRoot();

    if (!catalog_.load(root))
    {
        if (error != nullptr)
        {
            *error = catalog_.errorString();
        }

        return false;
    }

    stock_panel_->refresh();
    updateInfo();

    return true;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void MainWindow::editConsist(int index)
{
    if (index < 0 || index >= scenario_.size() || !catalog_.isLoaded())
    {
        return;
    }

    ConsistEditorDialog dialog(scenario_, route_, catalog_, scenario_.trains()[index],
                               index, this);

    if (dialog.exec() == QDialog::Accepted)
    {
        applyConsistEdit(index, dialog.edited(), dialog.cuts());
    }
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
bool MainWindow::applyConsistEdit(int index, const PlacedTrain &edited, const QVector<int> &cuts)
{
    if (index < 0 || index >= scenario_.size())
    {
        return false;
    }

    const EditPlan plan = planEdit(scenario_, route_, catalog_, index, edited, cuts);

    if (!plan.ok())
    {
        QMessageBox::warning(this, tr("Правка состава"),
                             tr("Правку применить нельзя:\n%1").arg(plan.errors.join(QLatin1Char('\n'))));
        return false;
    }

    pushUndo();

    scenario_.replace(index, plan.pieces.first());

    for (int i = 1; i < plan.pieces.size(); ++i)
    {
        scenario_.add(plan.pieces[i]);
    }

    refreshTrains();
    map_->update();

    if (plan.pieces.size() > 1)
    {
        statusBar()->showMessage(tr("Состав разделён на поездов: %1").arg(plan.pieces.size()), 6000);
    }

    return true;
}

//------------------------------------------------------------------------------
// Несохранённые изменения
//------------------------------------------------------------------------------
bool MainWindow::isModified() const
{
    return projectSnapshot(project_info_, scenario_) != clean_snapshot_;
}

void MainWindow::markClean()
{
    clean_snapshot_ = projectSnapshot(project_info_, scenario_);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    saveWindowState();

    if (confirmUnsavedChanges())
    {
        event->accept();
    }
    else
    {
        event->ignore();
    }
}

//------------------------------------------------------------------------------
// Подтверждение при несохранённых изменениях (выход и закрытие сценария)
//------------------------------------------------------------------------------
bool MainWindow::confirmUnsavedChanges()
{
    if (!isModified())
    {
        return true;
    }

    QMessageBox box(QMessageBox::Question, tr("Несохранённые изменения"),
                    tr("В сценарии есть несохранённые изменения. Сохранить проект?"),
                    QMessageBox::NoButton, this);
    QPushButton *save = box.addButton(tr("Сохранить"), QMessageBox::AcceptRole);
    QPushButton *discard = box.addButton(tr("Не сохранять"), QMessageBox::DestructiveRole);
    box.addButton(tr("Отмена"), QMessageBox::RejectRole);
    box.setDefaultButton(save);
    box.exec();

    if (box.clickedButton() == save)
    {
        return saveProject();
    }

    return box.clickedButton() == discard;
}

//------------------------------------------------------------------------------
// Закрытие сценария: подтверждение и возврат к стартовому состоянию
//------------------------------------------------------------------------------
void MainWindow::closeScenario()
{
    if (!confirmUnsavedChanges())
    {
        return;
    }

    resetToStartup();

    statusBar()->showMessage(tr("Сценарий закрыт. Откройте маршрут или проект."), 8000);
}

//------------------------------------------------------------------------------
// Сброс к стартовому состоянию «сценарий не открыт» (единый код с запуском
// программы: после закрытия программа выглядит и ведёт себя как при старте)
//------------------------------------------------------------------------------
void MainWindow::resetToStartup()
{
    // Открытые окна и режимы карты завершаются
    if (timetable_dialog_ != nullptr)
    {
        timetable_dialog_->close();
    }

    if (check_dialog_ != nullptr)
    {
        check_dialog_->close();
    }

    if (pick_index_ >= 0 || event_pick_)
    {
        pick_index_ = -1;
        event_pick_ = false;
        map_->setPickMode(false);
        statusBar()->clearMessage();
    }

    has_template_ = false;
    template_ = PlacedTrain();
    map_->setPlacementTemplate(nullptr);
    stock_panel_->clearSelection();

    // Маршрут и каталог игры тоже закрываются: новый сценарий начинается
    // с выбора маршрута, как при первом запуске
    route_ = Route();
    catalog_ = VehicleCatalog();
    catalog_.loadManualModules(manualModulesPath());

    scenario_.clear();
    project_info_ = ProjectInfo();
    project_path_.clear();

    map_->setRoute(&route_);
    map_->setTripOverlay({}, {});
    map_->setSignalMarks({});
    map_->setSelectedTrain(-1);

    stock_panel_->setCatalog(&catalog_);
    stock_panel_->refresh();

    refreshTrains();
    events_panel_->refresh();
    refreshAutoEvents();
    updateTripOverlay();

    hover_label_->clear();
    statusBar()->clearMessage();
    setWindowTitle(tr("Редактор сценариев RRS"));

    clearUndo();
    markClean();
    updateInfo();
}

//------------------------------------------------------------------------------
// Проект: сохранение и загрузка
//------------------------------------------------------------------------------
bool MainWindow::saveProject()
{
    return saveProjectAs(project_path_);
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
bool MainWindow::saveProjectAs(const QString &path_in)
{
    QString path = path_in;

    if (path.isEmpty())
    {
        QSettings settings(SETTINGS_ORG, SETTINGS_APP);
        const QString start = settings.value("lastProjectDir", QDir::homePath()).toString();

        path = QFileDialog::getSaveFileName(this, tr("Сохранить проект"),
                                            start + QLatin1Char('/') + (project_info_.scenarioName.isEmpty()
                                                ? tr("сценарий") : project_info_.scenarioName) + ".rrsproj",
                                            tr("Проекты сценариев (*.rrsproj)"));

        if (path.isEmpty())
        {
            return false;
        }

        if (!path.endsWith(QLatin1String(".rrsproj")))
        {
            path += QStringLiteral(".rrsproj");
        }
    }

    project_info_.routeDir = QDir(route_.dir()).absolutePath();
    project_info_.gameRoot = catalog_.gameRoot();

    QString error;

    if (!rse::saveProject(path, project_info_, scenario_, &error))
    {
        QMessageBox::critical(this, tr("Сохранение проекта"), error);
        return false;
    }

    project_path_ = path;
    markClean();

    QSettings settings(SETTINGS_ORG, SETTINGS_APP);
    settings.setValue("lastProjectDir", QFileInfo(path).absolutePath());

    statusBar()->showMessage(tr("Проект сохранён: %1").arg(path), 5000);

    return true;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
bool MainWindow::openProject(const QString &path)
{
    ProjectInfo info;
    Scenario loaded;
    QString error;

    if (!rse::loadProject(path, &info, &loaded, &error))
    {
        QMessageBox::critical(this, tr("Открытие проекта"), error);
        return false;
    }

    // Маршрут проекта
    const QString current = QDir(route_.dir()).absolutePath();

    if (!route_.isLoaded() || QDir(info.routeDir).absolutePath() != current)
    {
        if (!QFileInfo::exists(info.routeDir) || !openRoute(info.routeDir))
        {
            QMessageBox::warning(this, tr("Открытие проекта"),
                                 tr("Маршрут проекта не найден: %1\nОткройте маршрут вручную "
                                    "и повторите.").arg(info.routeDir));
            return false;
        }
    }

    if (!catalog_.isLoaded() && VehicleCatalog::isGameRoot(info.gameRoot))
    {
        openGame(info.gameRoot, false);
    }

    scenario_.clear();

    for (PlacedTrain t : loaded.trains())
    {
        if (catalog_.isLoaded())
        {
            updateSummary(t, catalog_);
        }

        scenario_.add(t);
    }

    project_info_ = info;
    project_path_ = path;
    clearUndo();
    markClean();

    QSettings settings(SETTINGS_ORG, SETTINGS_APP);
    settings.setValue("lastProjectDir", QFileInfo(path).absolutePath());

    refreshTrains();
    events_panel_->refresh();
    map_->update();
    updateTripOverlay();

    statusBar()->showMessage(tr("Проект открыт: %1 (поездов: %2)").arg(path).arg(scenario_.size()), 6000);

    return true;
}

//------------------------------------------------------------------------------
// Импорт сценария
//------------------------------------------------------------------------------
bool MainWindow::importScenarioDir(const QString &dir, bool interactive, QStringList *report)
{
    if (!route_.isLoaded() || !catalog_.isLoaded())
    {
        if (interactive)
        {
            QMessageBox::information(this, tr("Импорт"), tr("Откройте маршрут и каталог игры."));
        }

        return false;
    }

    if (interactive && (!scenario_.isEmpty() || !project_info_.events.isEmpty()))
    {
        if (QMessageBox::question(this, tr("Импорт"),
                                  tr("Текущая расстановка и события будут заменены. Продолжить?"))
            != QMessageBox::Yes)
        {
            return false;
        }
    }

    ImportResult res = importScenario(route_, catalog_, dir);

    if (!res.ok)
    {
        if (interactive)
        {
            QMessageBox::critical(this, tr("Импорт"), res.error);
        }

        if (report != nullptr)
        {
            *report << res.error;
        }

        return false;
    }

    res.info.routeDir = QDir(route_.dir()).absolutePath();
    res.info.gameRoot = catalog_.gameRoot();

    pushUndo();

    scenario_ = res.scenario;
    project_info_ = res.info;
    project_path_.clear();
    markClean();

    refreshTrains();
    events_panel_->refresh();
    map_->setSelectedTrain(-1);
    updateTripOverlay();
    map_->update();

    QStringList lines = res.notes;

    for (const QString &w : res.warnings)
    {
        lines << QStringLiteral("! ") + w;
    }

    if (report != nullptr)
    {
        *report = lines;
    }

    statusBar()->showMessage(tr("Сценарий «%1» импортирован").arg(res.info.scenarioName), 8000);

    if (interactive)
    {
        // Отчёт импорта с раскрываемым списком (аудит 4.5)
        QDialog report(this);
        report.setWindowTitle(tr("Импорт сценария «%1»").arg(res.info.scenarioName));
        report.setMinimumSize(520, 380);

        auto *report_layout = new QVBoxLayout(&report);

        auto *report_hint = new QLabel(tr("Расстановка и события заменены содержимым "
                                          "сценария. При экспорте под тем же именем включите "
                                          "замену: main.lua, README.md и расписания будут "
                                          "переписаны."), &report);
        report_hint->setWordWrap(true);
        report_layout->addWidget(report_hint);

        auto *tree = new QTreeWidget(&report);
        tree->setHeaderLabels({ tr("Группа"), tr("Сведения") });
        tree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        report_layout->addWidget(tree, 1);

        auto *trains_item = new QTreeWidgetItem(tree, { tr("Поезда"), QString::number(res.scenario.size()) });

        for (const PlacedTrain &t : res.scenario.trains())
        {
            new QTreeWidgetItem(trains_item, { t.name,
                                               t.timetable.valid ? tr("есть график")
                                                                 : tr("без графика") });
        }

        auto *events_item = new QTreeWidgetItem(tree, { tr("События"),
                                                        QString::number(res.info.events.size()) });

        for (const ScenarioEvent &e : res.info.events)
        {
            new QTreeWidgetItem(events_item, { e.title, e.traj });
        }

        auto *notes_item = new QTreeWidgetItem(tree, { tr("Замечания"),
                                                       QString::number(res.warnings.size()) });

        for (const QString &w : res.warnings)
        {
            new QTreeWidgetItem(notes_item, { QStringLiteral("⚠"), w });
        }

        new QTreeWidgetItem(tree, { tr("Прочее"), res.notes.join(QStringLiteral("; ")) });

        notes_item->setExpanded(!res.warnings.isEmpty());

        auto *ok_button = new QPushButton(tr("Понятно"), &report);
        connect(ok_button, &QPushButton::clicked, &report, &QDialog::accept);
        report_layout->addWidget(ok_button);

        report.exec();
    }

    return true;
}

//------------------------------------------------------------------------------
// Экспорт сценария
//------------------------------------------------------------------------------
void MainWindow::exportScenario()
{
    if (!route_.isLoaded() || !catalog_.isLoaded())
    {
        QMessageBox::information(this, tr("Экспорт"), tr("Откройте маршрут и каталог игры."));
        return;
    }

    ExportDialog dialog(route_, catalog_, scenario_, project_info_, this);

    if (dialog.exec() != QDialog::Accepted)
    {
        return;
    }

    const ExportOptions opt = dialog.options();
    ExportPlan plan;
    QString error;

    if (!exportScenarioTo(opt, &error, &plan))
    {
        QMessageBox::critical(this, tr("Экспорт"), error);
        return;
    }

    QString done = tr("Сценарий записан в %1").arg(QDir(route_.dir()).filePath(
        "scenarios/" + opt.info.scenarioName));

    // Предыдущая версия main.lua сохранилась рядом, если новая отличается
    if (!plan.mainBackupName.isEmpty()
        && QFileInfo::exists(QDir(plan.scenarioDir).filePath(plan.mainBackupName)))
    {
        done += tr("\n\nПредыдущая версия main.lua сохранена как %1.").arg(plan.mainBackupName);
    }

    QMessageBox::information(this, tr("Экспорт"), done);
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
bool MainWindow::exportScenarioTo(const ExportOptions &options, QString *error, ExportPlan *plan_out)
{
    const ExportPlan plan = planExport(route_, catalog_, scenario_, options);

    if (plan_out != nullptr)
    {
        *plan_out = plan;
    }

    if (!plan.ok())
    {
        if (error != nullptr)
        {
            *error = plan.errors.join(QLatin1Char('\n'));
        }

        return false;
    }

    if (!writeExport(route_, catalog_, scenario_, plan, options, error))
    {
        return false;
    }

    // Новые составы появились в cfg/trains
    const QString root = catalog_.gameRoot();

    if (!root.isEmpty())
    {
        openGame(root, false);
    }

    project_info_ = options.info;

    // Проект запоминает, что именно записал: так при следующем экспорте
    // распознаются ручные правки main.lua (этап 19, аудит 4.4)
    QFile main_file(QDir(plan.scenarioDir).filePath(QStringLiteral("main.lua")));

    if (main_file.open(QIODevice::ReadOnly))
    {
        project_info_.exportedMainSha256 = QString::fromLatin1(
            QCryptographicHash::hash(main_file.readAll(), QCryptographicHash::Sha256).toHex());
        project_info_.exportedAt = QDateTime::currentSecsSinceEpoch();
    }

    statusBar()->showMessage(tr("Сценарий «%1» экспортирован").arg(options.info.scenarioName), 8000);

    return true;
}

//------------------------------------------------------------------------------
// События сценария
//------------------------------------------------------------------------------
int MainWindow::effectiveStartTime() const
{
    return project_info_.startTime >= 0 ? project_info_.startTime : suggestStartTime(scenario_);
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void MainWindow::addEvent()
{
    ScenarioEvent e;

    e.time = effectiveStartTime() + 60;
    e.title = tr("Событие %1").arg(project_info_.events.size() + 1);

    editEvent(-1, &e);
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void MainWindow::editEvent(int index, const ScenarioEvent *resume, int select)
{
    if (!route_.isLoaded() || index >= project_info_.events.size())
    {
        return;
    }

    ScenarioEvent e;

    if (resume != nullptr)
    {
        e = *resume;
    }
    else if (index >= 0)
    {
        e = project_info_.events[index];
    }

    EventDialog dialog(route_, scenario_, e, effectiveStartTime(), select, this);
    const int code = dialog.exec();

    if (code == QDialog::Accepted)
    {
        pushUndo();

        if (index < 0)
        {
            project_info_.events.push_back(dialog.event());
            events_panel_->refresh();
            events_panel_->setCurrentIndex(project_info_.events.size() - 1);
        }
        else
        {
            project_info_.events[index] = dialog.event();
            events_panel_->refresh();
        }
    }
    else if (code == EventDialog::PickRequested)
    {
        event_pick_ = true;
        event_index_ = index;
        event_select_ = dialog.selectedAction();
        event_field_ = dialog.pickField();
        event_state_ = dialog.event();
        map_->setPickMode(true);
        statusBar()->showMessage(tr("Щёлкните по нужной траектории (Esc - отмена)"));
    }
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void MainWindow::removeEvent(int index)
{
    if (index < 0 || index >= project_info_.events.size())
    {
        return;
    }

    pushUndo();

    project_info_.events.removeAt(index);
    events_panel_->refresh();
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void MainWindow::moveEvent(int index, int delta)
{
    const int to = index + delta;

    if (index < 0 || to < 0 || to >= project_info_.events.size() || index >= project_info_.events.size())
    {
        return;
    }

    pushUndo();

    project_info_.events.swapItemsAt(index, to);
    events_panel_->refresh();
    events_panel_->setCurrentIndex(to);
}

//------------------------------------------------------------------------------
// График поезда
//------------------------------------------------------------------------------
void MainWindow::editTimetable(int index, const TimetableState *resume)
{
    if (index < 0 || index >= scenario_.size() || !route_.isLoaded() || !catalog_.isLoaded())
    {
        return;
    }

    const PlacedTrain &train = scenario_.trains()[index];

    TimetableState state = (resume != nullptr) ? *resume : TimetableState::fromTimetable(train.timetable);

    // Закреплённые станции трассы («через станцию») хранятся у поезда
    state.pins = train.pins;

    // Окно не модальное: карта остаётся доступной, а маршрут на ней перерисовывается
    // сразу при каждом изменении графика
    if (timetable_dialog_ != nullptr)
    {
        timetable_dialog_->close();
    }

    auto *dialog = new TimetableDialog(route_, catalog_, scenario_, index, state, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowModality(Qt::NonModal);
    timetable_dialog_ = dialog;
    timetable_index_ = index;

    connect(dialog, &TimetableDialog::tripChanged, this,
            [this](const QVector<PathSegment> &path, const QVector<TimetableStation> &stations)
            {
                map_->setTripOverlay(path, stations);
            });

    // Светофоры, которые откроются маршрутами графика, подсвечиваются на карте,
    // пока открыто окно графика
    connect(dialog, &TimetableDialog::signalMarksChanged, this,
            [this](const QVector<SignalMark> &marks) { map_->setSignalMarks(marks); });

    connect(dialog, &TimetableDialog::pickRequested, this, [this, index, dialog]
    {
        pick_index_ = index;
        map_->setPickMode(true);
        statusBar()->showMessage(tr("Щёлкните по пути, где поезд должен закончить движение "
                                    "(Esc - отмена). Окно графика остаётся открытым."));
        dialog->raise();
    });

    connect(dialog, &QDialog::finished, this, [this, dialog, index](int code)
    {
        // Закрепления трассы сохраняются при любом завершении с результатом:
        // и при применении графика, и при его удалении
        if (code == QDialog::Accepted || code == TimetableDialog::RemoveRequested)
        {
            pushUndo();

            PlacedTrain t = scenario_.trains()[index];
            t.pins = dialog->state().pins;
            t.trainClass = dialog->category();
            scenario_.replace(index, t);
        }

        if (code == QDialog::Accepted)
        {
            setTimetable(index, dialog->timetable());
        }
        else if (code == TimetableDialog::RemoveRequested)
        {
            setTimetable(index, Timetable());
        }

        if (pick_index_ == index)
        {
            pick_index_ = -1;
            map_->setPickMode(false);
            statusBar()->clearMessage();
        }

        timetable_dialog_ = nullptr;
        map_->setSignalMarks({});
        updateTripOverlay();
    });

    dialog->show();
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void MainWindow::finishPick(const Route::Hit &hit)
{
    if (!picking())
    {
        return;
    }

    if (event_pick_)
    {
        ScenarioEvent e = event_state_;
        const int idx = event_index_;
        const int sel = event_select_;

        event_pick_ = false;
        map_->setPickMode(false);
        statusBar()->clearMessage();

        if (hit.valid)
        {
            switch (event_field_)
            {
            case EventField::TriggerTraj: e.traj = hit.trajectory; break;
            case EventField::TriggerTraj2: e.traj2 = hit.trajectory; break;
            case EventField::ActionA:
                if (sel >= 0 && sel < e.actions.size()) e.actions[sel].a = hit.trajectory;
                break;
            case EventField::ActionB:
                if (sel >= 0 && sel < e.actions.size()) e.actions[sel].b = hit.trajectory;
                break;
            case EventField::None: break;
            }
        }

        editEvent(idx, &e, sel);
        return;
    }

    pick_index_ = -1;
    map_->setPickMode(false);
    statusBar()->clearMessage();

    if (hit.valid && timetable_dialog_ != nullptr)
    {
        timetable_dialog_->setEnd(hit.trajectory, hit.coord);
    }

    if (timetable_dialog_ != nullptr)
    {
        timetable_dialog_->raise();
        timetable_dialog_->activateWindow();
    }
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void MainWindow::setTimetable(int index, const Timetable &tt)
{
    if (index < 0 || index >= scenario_.size())
    {
        return;
    }

    PlacedTrain t = scenario_.trains()[index];
    t.timetable = tt;

    // Снимок отмены берётся в обработчике завершения окна графика - единый
    // снимок на «применить/убрать» вместе с закреплениями и категорией
    scenario_.replace(index, t);

    refreshTrains();
    updateTripOverlay();
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void MainWindow::updateTripOverlay()
{
    const int index = trains_panel_->currentIndex();

    if (index < 0 || index >= scenario_.size() || !scenario_.trains()[index].timetable.valid)
    {
        map_->setTripOverlay({}, {});
        return;
    }

    const PlacedTrain &t = scenario_.trains()[index];
    const PathResult path = findPath(route_, t.trajectory, t.coord, t.dir,
                                     t.timetable.endTrajectory, t.timetable.endCoord);

    if (path.found)
    {
        map_->setTripOverlay(path.segments, t.timetable.stations);
    }
    else
    {
        map_->setTripOverlay({}, {});
    }
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void MainWindow::removeTrain(int index)
{
    if (index < 0 || index >= scenario_.size())
    {
        return;
    }

    // Продолжения без локомотива или вагонов теряют смысл
    const QString gone = scenario_.trains()[index].name;
    QStringList dropped;

    for (int i = scenario_.size() - 1; i >= 0; --i)
    {
        const PlacedTrain &c = scenario_.trains()[i];

        if (i != index && c.isContinuation() && (c.formedFrom == gone || c.formedWagons == gone))
        {
            dropped << c.name;
        }
    }

    pushUndo();

    scenario_.remove(index);

    for (const QString &name : std::as_const(dropped))
    {
        scenario_.remove(scenario_.indexOfName(name));
    }

    refreshTrains();
    map_->setSelectedTrain(trains_panel_->currentIndex());
    map_->update();

    if (!dropped.isEmpty())
    {
        statusBar()->showMessage(tr("Вместе с поездом удалена сцепка: %1").arg(dropped.join(QStringLiteral(", "))), 8000);
    }
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void MainWindow::refreshAutoEvents()
{
    if (auto_events_panel_ == nullptr || !route_.isLoaded())
    {
        return;
    }

    QSet<QString> disabled;

    for (const PlacedTrain &t : scenario_.trains())
    {
        if (t.noAutoEvents)
        {
            disabled.insert(t.name);
        }
    }

    auto_events_panel_->setEvents(planAutoTriggers(route_, scenario_).events, disabled);
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void MainWindow::refreshTrains()
{
    syncContinuations();
    trains_panel_->refresh();

    if (auto_events_dock_ != nullptr && auto_events_dock_->isVisible())
    {
        refreshAutoEvents();
    }

    if (check_dialog_ != nullptr && check_dialog_->isVisible())
    {
        check_dialog_->recheck();
    }
}

//------------------------------------------------------------------------------
// Положение, состав и длина продолжения всегда выводятся из графика локомотива
// и вагонов. Если отправная точка сдвинулась, график продолжения сбрасывается
//------------------------------------------------------------------------------
void MainWindow::syncContinuations()
{
    if (!route_.isLoaded())
    {
        return;
    }

    for (int i = 0; i < scenario_.size(); ++i)
    {
        const PlacedTrain &c = scenario_.trains()[i];

        if (!c.isContinuation())
        {
            continue;
        }

        const FormationPlan plan = planFormation(route_, scenario_, c);

        if (!plan.ok)
        {
            continue;
        }

        PlacedTrain f = plan.formed;

        const bool moved = (f.trajectory != c.trajectory) || std::abs(f.coord - c.coord) > 0.05
                        || f.dir != c.dir || std::abs(f.length - c.length) > 0.05;

        if (!moved && f.groups.size() == c.groups.size())
        {
            continue;
        }

        if (moved && c.timetable.valid)
        {
            f.timetable = Timetable();
            statusBar()->showMessage(tr("Сцепка «%1»: точка прибытия локомотива изменилась, "
                                        "график после сцепки сброшен - задайте его заново.").arg(c.name), 10000);
        }

        scenario_.replace(i, f);
    }
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void MainWindow::editContinuation(int index)
{
    if (index < 0 || index >= scenario_.size() || !route_.isLoaded())
    {
        return;
    }

    const PlacedTrain current = scenario_.trains()[index];
    const bool editing = current.isContinuation();

    const int loco_index = editing ? scenario_.indexOfName(current.formedFrom) : index;

    if (loco_index < 0)
    {
        QMessageBox::warning(this, tr("Сцепка"), tr("Локомотив сцепки не найден."));
        return;
    }

    PlacedTrain seed = current;

    if (!editing)
    {
        seed = PlacedTrain();
        seed.formedFrom = current.name;
        seed.name = current.name + QStringLiteral(" (формирование)");

        if (scenario_.indexOfName(seed.name) >= 0)
        {
            seed.name = scenario_.uniqueName(seed.name);
        }

        // вагоны по умолчанию: ближайший стоящий состав без графика
        for (const PlacedTrain &t : scenario_.trains())
        {
            if (t.name != current.name && !t.isContinuation() && !t.timetable.valid)
            {
                seed.formedWagons = t.name;
                break;
            }
        }
    }

    ContinuationDialog dialog(route_, scenario_, loco_index, seed, editing ? index : -1, this);

    if (dialog.exec() != QDialog::Accepted)
    {
        return;
    }

    PlacedTrain result = dialog.result();

    if (editing)
    {
        // график сохраняется, если точка отправления не изменилась (это решает syncContinuations)
        result.timetable = current.timetable;
        pushUndo();
        scenario_.replace(index, result);
    }
    else
    {
        pushUndo();
        index = scenario_.add(result);
    }

    refreshTrains();
    trains_panel_->setCurrentIndex(scenario_.indexOfName(result.name));
    map_->update();

    statusBar()->showMessage(tr("Сцепка «%1» создана. Задайте его график кнопкой «График…».")
                                 .arg(result.name), 10000);
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
QVector<ScheduleIssue> MainWindow::checkResult() const
{
    CheckOptions opt;
    opt.startTime = effectiveStartTime();

    return checkSchedules(route_, scenario_, opt);
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void MainWindow::showScheduleCheck()
{
    if (!route_.isLoaded())
    {
        QMessageBox::information(this, tr("Проверка графиков"), tr("Сначала откройте маршрут."));
        return;
    }

    if (check_dialog_ == nullptr)
    {
        check_dialog_ = new CheckDialog([this] { return checkResult(); }, this);
        check_dialog_->setAttribute(Qt::WA_DeleteOnClose, false);

        connect(check_dialog_, &CheckDialog::issueActivated, this,
                [this](const QStringList &trains, const QString &)
        {
            if (!trains.isEmpty())
            {
                const int i = scenario_.indexOfName(trains.first());

                if (i >= 0)
                {
                    trains_panel_->setCurrentIndex(i);
                    const PlacedTrain &t = scenario_.trains()[i];
                    map_->centerOn(t.trajectory, t.coord);
                }
            }
        });

        // «Добавить стоянку для пропуска» из проверки (аудит 3.7)
        connect(check_dialog_, &CheckDialog::parkRequested, this, [this](const QString &train)
        {
            const int i = scenario_.indexOfName(train);

            if (i < 0)
            {
                return;
            }

            trains_dock_->show();
            trains_dock_->raise();
            trains_panel_->setCurrentIndex(i);
            editTimetable(i);
            statusBar()->showMessage(tr("Добавьте стоянку на станции, чтобы пропустить "
                                        "второй поезд: выберите станцию, режим «Остановка»"), 12000);
        });
    }

    check_dialog_->recheck();
    check_dialog_->show();
    check_dialog_->raise();
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void MainWindow::onManualModuleChanged()
{
    if (!catalog_.saveManualModules(manualModulesPath()))
    {
        QMessageBox::warning(this, tr("Не удалось сохранить привязки модулей"),
                             tr("Не удалось записать файл %1. Привязки действуют до "
                                "закрытия программы.").arg(manualModulesPath()));
    }

    updateInfo();
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
QStringList MainWindow::allWarnings() const
{
    QStringList all;

    if (route_.isLoaded())
    {
        for (const QString &w : route_.warnings())
        {
            all << tr("Маршрут: %1").arg(w);
        }
    }

    if (catalog_.isLoaded())
    {
        for (const QString &w : catalog_.warnings())
        {
            all << tr("Подвижной состав: %1").arg(w);
        }
    }

    return all;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void MainWindow::updateInfo()
{
    QStringList parts;

    if (route_.isLoaded())
    {
        parts << tr("Траекторий: %1   Стрелок: %2   Станций: %3")
                     .arg(route_.trajectories().size())
                     .arg(route_.switches().size())
                     .arg(route_.stations().size());
    }
    else
    {
        parts << tr("Маршрут не загружен");
    }

    // Счётчики подвижного состава показывает сама панель; в строке состояния
    // только то, чего нет в панели
    if (!catalog_.isLoaded())
    {
        parts << tr("Подвижной состав не загружен");
    }

    const int warnings = allWarnings().size();

    warnings_button_->setVisible(warnings > 0);
    warnings_button_->setText(tr("⚠ Замечаний: %1").arg(warnings));

    info_label_->setText(parts.join(QStringLiteral("   │   ")));
    warnings_action_->setEnabled(warnings > 0);

    updateWorkflowState();
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void MainWindow::onShowWarnings()
{
    const QStringList all = allWarnings();

    QMessageBox box(QMessageBox::Information, tr("Замечания при загрузке"),
                    warningsBreakdown(), QMessageBox::Ok, this);

    box.setDetailedText(all.join(QLatin1Char('\n')));
    box.exec();
}

//------------------------------------------------------------------------------
// Ссылка «Назначить модуль» из диалога поезда (аудит 2.4)
//------------------------------------------------------------------------------
void MainWindow::onAssignModuleRequested(const QString &config_dir)
{
    stock_dock_->show();
    stock_dock_->raise();
    stock_panel_->assignModuleForUnit(config_dir);
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void MainWindow::onHoverChanged(const Route::Hit &hit)
{
    if (!hit.valid)
    {
        hover_label_->clear();
        return;
    }

    const TrajectoryData *traj = route_.find(hit.trajectory);

    // Сдвоенная подпись: станция рядом с точкой + техническое имя пути (аудит 2.2)
    QString station;

    if (route_.isLoaded())
    {
        for (const StationData &sd : route_.stations())
        {
            if ((sd.pos - hit.pos).manhattanLength() < 400.0)
            {
                station = sd.name;
                break;
            }
        }
    }

    QString text;

    if (!station.isEmpty())
    {
        text += tr("%1 · ").arg(station);
    }

    text += tr("%1   координата %2 м")
                .arg(hit.trajectory)
                .arg(hit.coord, 0, 'f', 1);

    if (traj != nullptr)
    {
        text += tr(" из %1 м").arg(traj->length, 0, 'f', 1);
    }

    hover_label_->setText(text);
}

//------------------------------------------------------------------------------
// Панель шагов: щелчок по шагу (аудит 1.3)
//------------------------------------------------------------------------------
void MainWindow::onWorkflowStep(int step)
{
    switch (step)
    {
    case 1:
        onOpenRoute();
        break;

    case 2:
        stock_dock_->show();
        stock_dock_->raise();
        break;

    case 3:
        trains_dock_->show();
        trains_dock_->raise();
        break;

    case 4:
        if (trains_panel_->currentIndex() >= 0)
        {
            editTimetable(trains_panel_->currentIndex());
        }
        else if (scenario_.size() > 0)
        {
            editTimetable(0);
        }
        else
        {
            trains_dock_->show();
            trains_dock_->raise();
        }
        break;

    case 5:
        showScheduleCheck();
        break;

    case 6:
        exportScenario();
        break;

    default:
        break;
    }
}

//------------------------------------------------------------------------------
// Состояние шагов панели (аудит 1.3)
//------------------------------------------------------------------------------
void MainWindow::updateWorkflowState()
{
    if (workflow_ == nullptr)
    {
        return;
    }

    WorkflowBar::State st;
    st.route = route_.isLoaded();
    st.catalog = catalog_.isLoaded();
    st.trains = scenario_.size() > 0;

    for (const PlacedTrain &t : scenario_.trains())
    {
        st.timetables = st.timetables || t.timetable.valid;
    }

    st.exported = project_info_.exportedAt >= 0;

    workflow_->setState(st);
}

//------------------------------------------------------------------------------
// Справка по клавишам (аудит 1.5)
//------------------------------------------------------------------------------
QStringList MainWindow::shortcutHints() const
{
    QStringList lines;

    for (const QAction *menu_action : menuBar()->actions())
    {
        for (QAction *a : menu_action->menu()->actions())
        {
            if (!a->shortcut().isEmpty())
            {
                lines << tr("%1 — %2").arg(a->shortcut().toString(QKeySequence::NativeText),
                                          a->text().remove(QLatin1Char('&')));
            }
        }
    }

    lines << tr("Стрелки — перемещение по карте");
    lines << tr("+ / - — масштаб карты");
    lines << tr("Esc — отмена режима расстановки или выбора точки");

    return lines;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void MainWindow::onShowShortcuts()
{
    QMessageBox box(QMessageBox::Information, tr("Сочетания клавиш"),
                    tr("Клавиши редактора:"), QMessageBox::Ok, this);
    box.setDetailedText(shortcutHints().join(QLatin1Char('\n')));
    box.exec();
}

//------------------------------------------------------------------------------
// Разбивка замечаний загрузки (аудит 1.6)
//------------------------------------------------------------------------------
QString MainWindow::warningsBreakdown() const
{
    const QStringList all = allWarnings();

    int route_count = 0;
    int catalog_count = 0;

    for (const QString &w : all)
    {
        if (w.startsWith(QStringLiteral("Маршрут:")))
        {
            ++route_count;
        }
        else if (w.startsWith(QStringLiteral("Подвижной состав:")))
        {
            ++catalog_count;
        }
    }

    if (all.isEmpty())
    {
        return tr("Замечаний нет: данные маршрута и игры загружены полностью.");
    }

    return tr("Всего замечаний: %1 (маршрут: %2, подвижной состав: %3). "
              "Данные загружены, но могут быть неполными.")
               .arg(all.size()).arg(route_count).arg(catalog_count);
}

//------------------------------------------------------------------------------
// Каталоги маршрутов игры (аудит 4.6)
//------------------------------------------------------------------------------
QStringList MainWindow::scanRoutes(const QString &game_root)
{
    QStringList routes;

    const QDir routes_dir = QDir(game_root).filePath(QStringLiteral("routes"));

    for (const QFileInfo &entry : routes_dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot))
    {
        if (QFileInfo::exists(QDir(entry.absoluteFilePath())
                                  .filePath(QStringLiteral("topology/trajectories"))))
        {
            routes << entry.fileName();
        }
    }

    routes.sort(Qt::CaseInsensitive);
    return routes;
}

//------------------------------------------------------------------------------
// Стартовый экран (аудит 4.6)
//------------------------------------------------------------------------------
void MainWindow::maybeShowStartScreen()
{
    if (route_.isLoaded())
    {
        return;
    }

    QSettings settings(SETTINGS_ORG, SETTINGS_APP);

    if (!settings.value("showStartScreen", true).toBool())
    {
        return;
    }

    showStartScreen();
}

void MainWindow::showStartScreen()
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Добро пожаловать в редактор сценариев RRS"));
    dialog.setMinimumWidth(560);

    auto *layout = new QVBoxLayout(&dialog);

    auto *hint = new QLabel(tr("Укажите папку игры (рядом с папками cfg, routes, modules) — "
                               "редактор покажет установленные маршруты."), &dialog);
    hint->setWordWrap(true);
    layout->addWidget(hint);

    auto *game_row = new QHBoxLayout;
    auto *game_edit = new QLineEdit(catalog_.gameRoot(), &dialog);
    auto *browse = new QPushButton(tr("Папка игры…"), &dialog);
    game_row->addWidget(game_edit, 1);
    game_row->addWidget(browse);
    layout->addLayout(game_row);

    auto *routes_list = new QListWidget(&dialog);
    layout->addWidget(routes_list, 1);

    auto *remember = new QCheckBox(tr("Показывать при запуске"), &dialog);
    remember->setChecked(true);
    layout->addWidget(remember);

    auto *buttons = new QHBoxLayout;
    auto *other_route = new QPushButton(tr("Другой маршрут…"), &dialog);
    auto *open_button = new QPushButton(tr("Открыть маршрут"), &dialog);
    open_button->setDefault(true);
    auto *close_button = new QPushButton(tr("Закрыть"), &dialog);
    buttons->addWidget(other_route);
    buttons->addStretch(1);
    buttons->addWidget(open_button);
    buttons->addWidget(close_button);
    layout->addLayout(buttons);

    const auto fill_list = [this, game_edit, routes_list]
    {
        routes_list->clear();
        routes_list->addItems(scanRoutes(game_edit->text().trimmed()));
    };

    fill_list();

    connect(browse, &QPushButton::clicked, this, [this, game_edit, &fill_list]
    {
        const QString dir = QFileDialog::getExistingDirectory(
            this, tr("Корневой каталог игры"), game_edit->text().trimmed());

        if (!dir.isEmpty())
        {
            game_edit->setText(dir);
            fill_list();
        }
    });

    connect(other_route, &QPushButton::clicked, this, [this, &dialog]
    {
        dialog.reject();
        onOpenRoute();
    });

    connect(close_button, &QPushButton::clicked, &dialog, &QDialog::reject);

    const auto open_selected = [this, game_edit, routes_list, remember, &dialog]
    {
        if (routes_list->currentItem() == nullptr)
        {
            return;
        }

        QSettings settings(SETTINGS_ORG, SETTINGS_APP);
        settings.setValue("showStartScreen", remember->isChecked());

        const QString route_dir = QDir(game_edit->text().trimmed())
                                      .filePath(QStringLiteral("routes/")
                                                + routes_list->currentItem()->text());

        dialog.accept();
        openRoute(route_dir);
    };

    connect(open_button, &QPushButton::clicked, open_selected);
    connect(routes_list, &QListWidget::itemDoubleClicked, open_selected);

    dialog.exec();
}

//------------------------------------------------------------------------------
// Размеры и видимость панелей (аудит 1.7)
//------------------------------------------------------------------------------
void MainWindow::saveWindowState()
{
    QSettings settings(SETTINGS_ORG, SETTINGS_APP);
    settings.setValue("windowState", saveState());
    settings.setValue("windowGeometry", saveGeometry());
}

void MainWindow::restoreWindowState()
{
    QSettings settings(SETTINGS_ORG, SETTINGS_APP);
    const QByteArray state = settings.value("windowState").toByteArray();

    if (!state.isEmpty())
    {
        restoreState(state);
    }

    const QByteArray geometry = settings.value("windowGeometry").toByteArray();

    if (!geometry.isEmpty())
    {
        restoreGeometry(geometry);
    }
}

//------------------------------------------------------------------------------
// Отмена и повтор (этап 24): снимки projectSnapshot до каждого изменения
//------------------------------------------------------------------------------
void MainWindow::pushUndo()
{
    undo_stack_.push_back(projectSnapshot(project_info_, scenario_));

    while (undo_stack_.size() > undo_limit_)
    {
        undo_stack_.removeFirst();
    }

    redo_stack_.clear();
    updateUndoActions();
}

void MainWindow::clearUndo()
{
    undo_stack_.clear();
    redo_stack_.clear();
    updateUndoActions();
}

void MainWindow::restoreSnapshot(const QByteArray &snapshot)
{
    ProjectInfo info;
    Scenario restored;
    QString error;

    if (!loadProjectData(snapshot, &info, &restored, &error))
    {
        QMessageBox::warning(this, tr("Отменить"), error);
        return;
    }

    // Пути маршрута и игры в снимке не сохраняются: они остались от открытого
    info.routeDir = project_info_.routeDir;
    info.gameRoot = project_info_.gameRoot;

    project_info_ = info;
    scenario_ = restored;

    refreshTrains();
    events_panel_->refresh();
    trains_panel_->setCurrentIndex(-1);
    map_->setSelectedTrain(-1);
    updateTripOverlay();
    map_->update();
    updateInfo();
}

void MainWindow::undo()
{
    if (undo_stack_.isEmpty())
    {
        return;
    }

    redo_stack_.push_back(projectSnapshot(project_info_, scenario_));
    restoreSnapshot(undo_stack_.takeLast());
    updateUndoActions();

    statusBar()->showMessage(tr("Действие отменено"), 3000);
}

void MainWindow::redo()
{
    if (redo_stack_.isEmpty())
    {
        return;
    }

    undo_stack_.push_back(projectSnapshot(project_info_, scenario_));
    restoreSnapshot(redo_stack_.takeLast());
    updateUndoActions();

    statusBar()->showMessage(tr("Действие повторено"), 3000);
}

void MainWindow::updateUndoActions()
{
    if (undo_action_ != nullptr)
    {
        undo_action_->setEnabled(canUndo());
    }

    if (redo_action_ != nullptr)
    {
        redo_action_->setEnabled(canRedo());
    }
}

} // namespace rse
