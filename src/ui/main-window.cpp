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
#include    <QDir>
#include    <QDockWidget>
#include    <QFileDialog>
#include    <QLabel>
#include    <QMenuBar>
#include    <QMessageBox>
#include    <QSettings>
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

    map_ = new MapWidget(this);
    setCentralWidget(map_);

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
    trains_dock_->raise();

    map_->setScenario(&scenario_);

    hover_label_ = new QLabel(this);
    info_label_ = new QLabel(this);

    statusBar()->addWidget(hover_label_, 1);
    statusBar()->addPermanentWidget(info_label_);

    // Ручные привязки модулей переживают перезапуск программы и смену маршрута
    catalog_.loadManualModules(manualModulesPath());

    createActions();
    updateInfo();

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

    connect(trains_panel_, &TrainsPanel::removeRequested, this, &MainWindow::removeTrain);
    connect(trains_panel_, &TrainsPanel::reverseRequested, this, &MainWindow::reverseTrain);
    connect(trains_panel_, &TrainsPanel::editRequested, this, &MainWindow::editTrain);
    connect(trains_panel_, &TrainsPanel::consistRequested, this, &MainWindow::editConsist);
    connect(trains_panel_, &TrainsPanel::timetableRequested, this,
            [this](int index) { editTimetable(index); });
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

    file_menu->addSeparator();

    QAction *quit = file_menu->addAction(tr("В&ыход"));
    quit->setShortcut(QKeySequence::Quit);
    connect(quit, &QAction::triggered, this, &QWidget::close);

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
    trains_panel_->refresh();
    events_panel_->refresh();

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

    if (dialog.exec() != QDialog::Accepted)
    {
        return;
    }

    const int index = scenario_.add(dialog.result());

    trains_panel_->refresh();
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

    scenario_.replace(index, t);
    trains_panel_->refresh();
    map_->update();
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

    if (dialog.exec() == QDialog::Accepted)
    {
        const PlacedTrain old = scenario_.trains()[index];
        PlacedTrain t = dialog.result();

        if (t.trajectory != old.trajectory || t.coord != old.coord || t.dir != old.dir)
        {
            t.timetable = Timetable();
        }

        scenario_.replace(index, t);
        trains_panel_->refresh();
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

    scenario_.replace(index, plan.pieces.first());

    for (int i = 1; i < plan.pieces.size(); ++i)
    {
        scenario_.add(plan.pieces[i]);
    }

    trains_panel_->refresh();
    map_->update();

    if (plan.pieces.size() > 1)
    {
        statusBar()->showMessage(tr("Состав разделён на поездов: %1").arg(plan.pieces.size()), 6000);
    }

    return true;
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

    QSettings settings(SETTINGS_ORG, SETTINGS_APP);
    settings.setValue("lastProjectDir", QFileInfo(path).absolutePath());

    trains_panel_->refresh();
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

    scenario_ = res.scenario;
    project_info_ = res.info;
    project_path_.clear();

    trains_panel_->refresh();
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
        QMessageBox::information(this, tr("Импорт сценария"),
                                 lines.join(QLatin1Char('\n'))
                                 + tr("\n\nПри экспорте под тем же именем включите замену: файлы "
                                      "main.lua, README.md и расписания будут переписаны."));
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
    QString error;

    if (!exportScenarioTo(opt, &error))
    {
        QMessageBox::critical(this, tr("Экспорт"), error);
        return;
    }

    QMessageBox::information(this, tr("Экспорт"),
                             tr("Сценарий записан в %1").arg(QDir(route_.dir()).filePath(
                                 "scenarios/" + opt.info.scenarioName)));
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
bool MainWindow::exportScenarioTo(const ExportOptions &options, QString *error)
{
    const ExportPlan plan = planExport(route_, catalog_, scenario_, options);

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

    TimetableDialog dialog(route_, catalog_, scenario_, index, state, this);
    const int code = dialog.exec();

    switch (code)
    {
    case QDialog::Accepted:
        setTimetable(index, dialog.timetable());
        break;

    case TimetableDialog::RemoveRequested:
        setTimetable(index, Timetable());
        break;

    case TimetableDialog::PickRequested:
        pick_index_ = index;
        pick_state_ = dialog.state();
        map_->setPickMode(true);
        statusBar()->showMessage(tr("Щёлкните по пути, где поезд должен закончить движение "
                                    "(Esc - отмена)"));
        break;

    default:
        break;
    }
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

    const int index = pick_index_;
    TimetableState state = pick_state_;

    pick_index_ = -1;
    map_->setPickMode(false);
    statusBar()->clearMessage();

    if (hit.valid)
    {
        state.hasEnd = true;
        state.endTrajectory = hit.trajectory;
        state.endCoord = hit.coord;
        state.endName.clear();
    }

    editTimetable(index, &state);
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
    scenario_.replace(index, t);

    trains_panel_->refresh();
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

    scenario_.remove(index);
    trains_panel_->refresh();
    map_->setSelectedTrain(trains_panel_->currentIndex());
    map_->update();
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

    if (warnings > 0)
    {
        parts << tr("Замечаний: %1").arg(warnings);
    }

    info_label_->setText(parts.join(QStringLiteral("   │   ")));
    warnings_action_->setEnabled(warnings > 0);
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void MainWindow::onShowWarnings()
{
    const QStringList all = allWarnings();

    QMessageBox box(QMessageBox::Information, tr("Замечания при загрузке"),
                    tr("Найдено замечаний: %1. Данные загружены, но могут "
                       "быть неполными.").arg(all.size()), QMessageBox::Ok, this);

    box.setDetailedText(all.join(QLatin1Char('\n')));
    box.exec();
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

    QString text = tr("%1   координата %2 м")
                       .arg(hit.trajectory)
                       .arg(hit.coord, 0, 'f', 1);

    if (traj != nullptr)
    {
        text += tr(" из %1 м").arg(traj->length, 0, 'f', 1);
    }

    hover_label_->setText(text);
}

} // namespace rse
