#include    "ui/rolling-stock-panel.h"

#include    <QButtonGroup>
#include    <QHBoxLayout>
#include    <QHeaderView>
#include    <QPushButton>
#include    <QInputDialog>
#include    <QLabel>
#include    <QLineEdit>
#include    <QMenu>
#include    <QToolButton>
#include    <QTreeWidget>
#include    <QVBoxLayout>

#include    <algorithm>
#include    <functional>

namespace rse
{

namespace
{

/// Роли данных строки дерева
constexpr int ROLE_KIND = Qt::UserRole;
constexpr int ROLE_CONFIG_DIR = Qt::UserRole + 1;
constexpr int ROLE_MODULE = Qt::UserRole + 2;

enum Kind
{
    KindVehicle = 1,
    KindTrain = 2,
    KindGroup = 3
};

enum Column
{
    ColName = 0,
    ColLength,
    ColMass,
    ColInfo,
    ColCount
};

//------------------------------------------------------------------------------
// Длина в метрах, масса в тоннах - как их привыкли видеть диспетчеры
//------------------------------------------------------------------------------
QString lengthText(double m)
{
    return QStringLiteral("%1 м").arg(m, 0, 'f', 1);
}

QString massText(double kg)
{
    return QStringLiteral("%1 т").arg(kg / 1000.0, 0, 'f', 1);
}

} // namespace

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
RollingStockPanel::RollingStockPanel(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);

    filter_edit_ = new QLineEdit(this);
    filter_edit_->setPlaceholderText(tr("Поиск по названию…"));
    filter_edit_->setClearButtonEnabled(true);

    auto *top_row = new QHBoxLayout;
    auto *new_train = new QPushButton(tr("Новый состав…"), this);
    new_train->setToolTip(tr("Собрать состав из любых единиц и сохранить в cfg/trains"));
    top_row->addWidget(filter_edit_, 1);
    top_row->addWidget(new_train);
    layout->addLayout(top_row);

    connect(new_train, &QPushButton::clicked, this, [this] { emit newTrainRequested(QString(), QString()); });

    // Вкладки «Единицы» / «Готовые составы» (аудит 2.7)
    auto *tabs_row = new QHBoxLayout;
    auto *units_tab = new QToolButton(this);
    units_tab->setText(tr("Единицы"));
    units_tab->setCheckable(true);
    units_tab->setChecked(true);
    units_tab->setToolTip(tr("Единицы подвижного состава из cfg/vehicles"));

    auto *trains_tab = new QToolButton(this);
    trains_tab->setText(tr("Готовые составы"));
    trains_tab->setCheckable(true);
    trains_tab->setToolTip(tr("Готовые составы из cfg/trains"));

    auto *tabs_group = new QButtonGroup(this);
    tabs_group->addButton(units_tab, 0);
    tabs_group->addButton(trains_tab, 1);
    tabs_group->setExclusive(true);

    tabs_row->addWidget(units_tab);
    tabs_row->addWidget(trains_tab);
    tabs_row->addStretch(1);
    layout->addLayout(tabs_row);

    connect(tabs_group, &QButtonGroup::idClicked, this, &RollingStockPanel::setActiveTab);

    tree_ = new QTreeWidget(this);
    tree_->setColumnCount(ColCount);
    tree_->setHeaderLabels({ tr("Название"), tr("Длина"), tr("Масса"), tr("Тип / модуль") });
    tree_->setUniformRowHeights(true);
    tree_->setAlternatingRowColors(true);
    tree_->setContextMenuPolicy(Qt::CustomContextMenu);
    tree_->header()->setStretchLastSection(true);
    tree_->header()->setSectionResizeMode(ColName, QHeaderView::Interactive);
    tree_->setColumnWidth(ColName, 190);
    tree_->setColumnWidth(ColLength, 70);
    tree_->setColumnWidth(ColMass, 70);
    layout->addWidget(tree_, 1);

    summary_label_ = new QLabel(this);
    summary_label_->setWordWrap(true);
    layout->addWidget(summary_label_);

    connect(filter_edit_, &QLineEdit::textChanged, this, &RollingStockPanel::setFilter);
    connect(tree_, &QWidget::customContextMenuRequested, this, &RollingStockPanel::onContextMenu);
    connect(tree_, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem *current) { onCurrentItemChanged(current); });

    refresh();
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void RollingStockPanel::setCatalog(VehicleCatalog *catalog)
{
    catalog_ = catalog;
    refresh();
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
QString RollingStockPanel::sourceText(ModuleSource source)
{
    switch (source)
    {
    case ModuleSource::OwnConfig:
        return QObject::tr("из конфига единицы (RRS 1.10)");

    case ModuleSource::TrainExact:
        return QObject::tr("из состава");

    case ModuleSource::TrainDir:
        return QObject::tr("по каталогу составов");

    case ModuleSource::TrainMajority:
        return QObject::tr("чаще в составах");

    case ModuleSource::Manual:
        return QObject::tr("назначен вручную");

    case ModuleSource::Unknown:
    default:
        return QObject::tr("не определён");
    }
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void RollingStockPanel::refresh()
{
    tree_->clear();

    if (catalog_ == nullptr || !catalog_->isLoaded())
    {
        summary_label_->setText(tr("Каталог не загружен. Откройте маршрут или укажите "
                                   "каталог игры: Файл → Указать каталог игры…"));
        return;
    }

    fillVehicles();
    fillTrains();

    QString text = tr("Единиц: %1, составов: %2").arg(catalog_->vehicles().size())
                                                  .arg(catalog_->trains().size());

    const int unresolved = catalog_->unresolvedCount();

    if (unresolved > 0)
    {
        text += tr("\nБез модуля: %1 (правая кнопка → Назначить модуль…)").arg(unresolved);
    }

    summary_label_->setText(text);

    applyFilter();
    applyTabFilter();
}

//------------------------------------------------------------------------------
// Вкладка списка: скрывать строки другой вкладки (аудит 2.7)
//------------------------------------------------------------------------------
void RollingStockPanel::setActiveTab(int tab)
{
    tab_ = (tab == 1) ? 1 : 0;
    applyTabFilter();
}

//------------------------------------------------------------------------------
// Скрыть готовые составы (вкладка «Единицы») или единицы (вкладка
// «Готовые составы»). Корневые группы скрываются целиком, если в их поддереве
// нет элементов активной вкладки
//------------------------------------------------------------------------------
void RollingStockPanel::applyTabFilter()
{
    std::function<bool(QTreeWidgetItem *)> has_trains = [&](QTreeWidgetItem *item) -> bool
    {
        if (item->data(ColName, ROLE_KIND).toInt() == KindTrain)
        {
            return true;
        }

        for (int i = 0; i < item->childCount(); ++i)
        {
            if (has_trains(item->child(i)))
            {
                return true;
            }
        }

        return false;
    };

    for (int i = 0; i < tree_->topLevelItemCount(); ++i)
    {
        QTreeWidgetItem *root = tree_->topLevelItem(i);
        const bool trains = has_trains(root);

        tree_->setRowHidden(i, QModelIndex(), (tab_ == 1) ? !trains : trains);
    }
}

//------------------------------------------------------------------------------
// Единицы, сгруппированные по каталогам конфигов
//------------------------------------------------------------------------------
void RollingStockPanel::fillVehicles()
{
    auto *root = new QTreeWidgetItem(tree_);
    root->setText(ColName, tr("Единицы подвижного состава"));
    root->setFirstColumnSpanned(true);

    QFont bold = root->font(ColName);
    bold.setBold(true);
    root->setFont(ColName, bold);

    QTreeWidgetItem *dir_item = nullptr;
    QString cur_dir;

    for (const VehicleConfigData &v : catalog_->vehicles())
    {
        if (dir_item == nullptr || v.configDir != cur_dir)
        {
            cur_dir = v.configDir;
            dir_item = new QTreeWidgetItem(root);
            dir_item->setText(ColName, v.configDir);
            dir_item->setData(ColName, ROLE_KIND, KindGroup);
            dir_item->setData(ColName, ROLE_CONFIG_DIR, v.configDir);

            // Единиц в игре немного: каталоги сразу раскрыты
            dir_item->setExpanded(true);
        }

        auto *item = new QTreeWidgetItem(dir_item);
        item->setText(ColName, v.config);
        item->setText(ColLength, lengthText(v.length));

        // В колонке - масса порожней единицы, подробности во всплывающей подсказке
        item->setText(ColMass, massText(v.emptyMass));
        item->setToolTip(ColMass, tr("Порожняя: %1\nГруз при полной загрузке: %2\n"
                                     "Всего с грузом: %3")
                                      .arg(massText(v.emptyMass), massText(v.payloadMass),
                                           massText(v.emptyMass + v.payloadMass)));

        QString info = v.isLocomotive ? tr("локомотив") : tr("вагон");

        if (v.hasAutopilot)
        {
            info += tr(" (авто)");
            item->setToolTip(ColInfo, tr("(авто): в конфиге есть модуль автопилота, "
                                         "возможно автоведение"));
        }

        // Не QLatin1String: символ не из ASCII превратился бы в "Â·"
        info += QStringLiteral(" · ");

        if (v.module.isValid())
        {
            info += v.module.id() + QLatin1String(" (") + sourceText(v.moduleSource)
                  + QLatin1Char(')');
        }
        else
        {
            info += tr("модуль не определён");
            item->setForeground(ColInfo, QColor(170, 40, 40));
            item->setToolTip(ColInfo, tr("Не удалось определить, какой модуль загружает эту "
                                         "единицу. Правая кнопка → Назначить модуль…"));
        }

        item->setText(ColInfo, info);
        item->setToolTip(ColName, v.id());
        item->setData(ColName, ROLE_KIND, KindVehicle);
        item->setData(ColName, ROLE_CONFIG_DIR, v.configDir);
        item->setData(ColName, ROLE_MODULE, v.module.id());
    }

    root->setExpanded(true);
}

//------------------------------------------------------------------------------
// Готовые составы с итогами: число единиц, длина, масса
//------------------------------------------------------------------------------
void RollingStockPanel::fillTrains()
{
    auto *root = new QTreeWidgetItem(tree_);
    root->setText(ColName, tr("Готовые составы (cfg/trains)"));
    root->setFirstColumnSpanned(true);

    QFont bold = root->font(ColName);
    bold.setBold(true);
    root->setFont(ColName, bold);

    for (const TrainConfigData &t : catalog_->trains())
    {
        const TrainSummary s = catalog_->summarize(t);

        auto *item = new QTreeWidgetItem(root);
        item->setText(ColName, t.name);
        item->setText(ColLength, lengthText(s.length));
        item->setText(ColMass, massText(s.mass));

        // Коротко, чтобы строка помещалась в узкую панель: «47 ед., лок. 1, авто»
        QString info = tr("%1 ед.").arg(s.vehicles);

        if (s.locomotives > 0)
        {
            info += tr(", лок. %1").arg(s.locomotives);
        }

        if (s.autopilotCapable)
        {
            info += tr(", авто");
        }

        if (!s.complete)
        {
            info += tr(" · неполные данные");
            item->setForeground(ColInfo, QColor(170, 40, 40));
        }

        item->setText(ColInfo, info);

        QString tip = t.title;

        if (!t.description.isEmpty())
        {
            tip += QLatin1Char('\n') + t.description;
        }

        if (s.autopilotCapable)
        {
            tip += tr("\n\nАвтоведение возможно: крайняя единица состава имеет модуль автопилота");
        }
        else if (s.locomotives > 0)
        {
            tip += tr("\n\nАвтоведение недоступно: у крайних единиц состава нет автопилота");
        }

        item->setToolTip(ColName, tip);
        item->setToolTip(ColInfo, tip);
        item->setData(ColName, ROLE_KIND, KindTrain);

        // Состав раскрывается в группы единиц
        for (const ConsistGroup &g : t.groups)
        {
            auto *gi = new QTreeWidgetItem(item);
            gi->setText(ColName, QStringLiteral("%1 × %2").arg(g.count).arg(g.config));

            const VehicleConfigData *v = catalog_->findVehicle(g.configDir, g.config);

            if (v != nullptr)
            {
                gi->setText(ColLength, lengthText(g.count * v->length));
                gi->setText(ColMass, massText(g.count * (v->emptyMass
                    + v->payloadMass * std::clamp(g.payloadCoeff, 0.0, 1.0))));
            }
            else
            {
                gi->setText(ColInfo, tr("нет в cfg/vehicles"));
                gi->setForeground(ColInfo, QColor(170, 40, 40));
            }

            gi->setData(ColName, ROLE_KIND, KindGroup);
        }
    }

    root->setExpanded(true);
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void RollingStockPanel::setFilter(const QString &text)
{
    if (filter_edit_->text() != text)
    {
        filter_edit_->setText(text);
        return;
    }

    applyFilter();
}

//------------------------------------------------------------------------------
// Строка видна, если подходит сама, или подходит её родитель (каталог, состав),
// или подходит любой потомок
//------------------------------------------------------------------------------
void RollingStockPanel::applyFilter()
{
    const QString needle = filter_edit_->text().trimmed();

    std::function<bool(QTreeWidgetItem *, bool)> apply =
        [&](QTreeWidgetItem *item, bool parent_matches) -> bool
    {
        const bool self = needle.isEmpty()
                       || item->text(ColName).contains(needle, Qt::CaseInsensitive);

        bool any_child = false;

        for (int i = 0; i < item->childCount(); ++i)
        {
            if (apply(item->child(i), parent_matches || self))
            {
                any_child = true;
            }
        }

        const bool visible = self || parent_matches || any_child;

        item->setHidden(!visible);

        if (!needle.isEmpty() && any_child)
        {
            item->setExpanded(true);
        }

        return visible;
    };

    for (int i = 0; i < tree_->topLevelItemCount(); ++i)
    {
        apply(tree_->topLevelItem(i), false);
    }
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
int RollingStockPanel::visibleVehicleCount() const
{
    int n = 0;

    std::function<void(QTreeWidgetItem *)> walk = [&](QTreeWidgetItem *item)
    {
        if (item->isHidden())
        {
            return;
        }

        if (item->data(ColName, ROLE_KIND).toInt() == KindVehicle)
        {
            ++n;
        }

        for (int i = 0; i < item->childCount(); ++i)
        {
            walk(item->child(i));
        }
    };

    for (int i = 0; i < tree_->topLevelItemCount(); ++i)
    {
        walk(tree_->topLevelItem(i));
    }

    return n;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
int RollingStockPanel::visibleTrainCount() const
{
    int n = 0;

    std::function<void(QTreeWidgetItem *)> walk = [&](QTreeWidgetItem *item)
    {
        if (item->isHidden())
        {
            return;
        }

        if (item->data(ColName, ROLE_KIND).toInt() == KindTrain)
        {
            ++n;
        }

        for (int i = 0; i < item->childCount(); ++i)
        {
            walk(item->child(i));
        }
    };

    for (int i = 0; i < tree_->topLevelItemCount(); ++i)
    {
        walk(tree_->topLevelItem(i));
    }

    return n;
}

//------------------------------------------------------------------------------
// Контекстное меню единицы или каталога конфигов: назначить модуль вручную
//------------------------------------------------------------------------------
void RollingStockPanel::onContextMenu(const QPoint &pos)
{
    if (catalog_ == nullptr || !catalog_->isLoaded())
    {
        return;
    }

    QTreeWidgetItem *item = tree_->itemAt(pos);

    if (item == nullptr)
    {
        return;
    }

    const QString config_dir = item->data(ColName, ROLE_CONFIG_DIR).toString();
    const int kind = item->data(ColName, ROLE_KIND).toInt();

    // Готовый состав: правка и копия
    if (kind == KindTrain || (kind == KindGroup && item->parent() != nullptr
        && item->parent()->data(ColName, ROLE_KIND).toInt() == KindTrain))
    {
        QTreeWidgetItem *train_item = (kind == KindTrain) ? item : item->parent();
        const QString name = train_item->text(ColName);

        QMenu train_menu(this);
        QAction *edit = train_menu.addAction(tr("Редактировать состав «%1»…").arg(name));
        QAction *copy = train_menu.addAction(tr("Сохранить копию под новым именем…"));

        QAction *picked = train_menu.exec(tree_->viewport()->mapToGlobal(pos));

        if (picked == edit)
        {
            emit editTrainRequested(name, false);
        }
        else if (picked == copy)
        {
            emit editTrainRequested(name, true);
        }

        return;
    }

    if (config_dir.isEmpty())
    {
        return;
    }

    QMenu menu(this);

    QAction *start_train = nullptr;

    if (kind == KindVehicle)
    {
        start_train = menu.addAction(tr("Создать состав с «%1»…").arg(item->text(ColName)));
        menu.addSeparator();
    }

    QAction *assign = menu.addAction(tr("Назначить модуль каталогу «%1»…").arg(config_dir));
    QAction *reset = nullptr;

    if (catalog_->manualModules().contains(config_dir))
    {
        reset = menu.addAction(tr("Снять ручное назначение"));
    }

    QAction *chosen = menu.exec(tree_->viewport()->mapToGlobal(pos));

    if (chosen == nullptr)
    {
        return;
    }

    if (chosen == start_train && start_train != nullptr)
    {
        emit newTrainRequested(config_dir, item->text(ColName));
        return;
    }

    if (chosen == reset)
    {
        catalog_->setManualModule(config_dir, ModuleRef());
        refresh();
        emit manualModuleChanged();
        return;
    }

    if (chosen == assign)
    {
        assignModuleForUnit(config_dir);
    }
}

//------------------------------------------------------------------------------
// Диалог назначения модуля каталогу конфигов (аудит 2.4). Отдельный метод,
// чтобы открыть его и ссылкой «Назначить модуль» из диалога поезда
//------------------------------------------------------------------------------
void RollingStockPanel::assignModuleForUnit(const QString &config_dir)
{
    if (catalog_ == nullptr || !catalog_->isLoaded() || config_dir.isEmpty())
    {
        return;
    }

    // Формат ввода - как в конфиге состава: «каталог/модуль» или просто «модуль»
    QStringList choices = catalog_->installedModules();
    const QString current = catalog_->manualModules().value(config_dir).id();

    bool ok = false;
    const QString text = QInputDialog::getItem(
        this, tr("Модуль для каталога «%1»").arg(config_dir),
        tr("Модуль подвижного состава (каталог/модуль, как ModuleDir и Module "
           "в конфиге состава):"),
        choices, std::max(0, static_cast<int>(choices.indexOf(current))), true, &ok);

    if (!ok || text.trimmed().isEmpty())
    {
        return;
    }

    ModuleRef ref;
    const QString t = text.trimmed();
    const int slash = t.indexOf(QLatin1Char('/'));

    if (slash >= 0)
    {
        ref.moduleDir = t.left(slash).trimmed();
        ref.module = t.mid(slash + 1).trimmed();
    }
    else
    {
        ref.module = t;
        ref.moduleDir = t;
    }

    if (!ref.isValid())
    {
        return;
    }

    catalog_->setManualModule(config_dir, ref);
    refresh();
    emit manualModuleChanged();
}

} // namespace rse

namespace rse
{

//------------------------------------------------------------------------------
// Выбор строки: готовый состав (или его группа) либо одна единица. Из них
// собирается заготовка поезда для расстановки по карте
//------------------------------------------------------------------------------
void RollingStockPanel::onCurrentItemChanged(QTreeWidgetItem *item)
{
    if (item != nullptr && item->parent() != nullptr
        && item->data(ColName, ROLE_KIND).toInt() == KindGroup
        && item->parent()->data(ColName, ROLE_KIND).toInt() == KindTrain)
    {
        item = item->parent();
    }

    if (catalog_ == nullptr || !catalog_->isLoaded() || item == nullptr)
    {
        emit consistCleared();
        return;
    }

    PlacedTrain train;

    const int kind = item->data(ColName, ROLE_KIND).toInt();

    if (kind == KindTrain)
    {
        const TrainConfigData *t = catalog_->findTrain(item->text(ColName));

        if (t == nullptr)
        {
            emit consistCleared();
            return;
        }

        train.title = t->name;
        train.sourceTrain = t->name;
        train.groups = t->groups;
    }
    else if (kind == KindVehicle)
    {
        const QString dir = item->data(ColName, ROLE_CONFIG_DIR).toString();
        const VehicleConfigData *v = catalog_->findVehicle(dir, item->text(ColName));

        if (v == nullptr)
        {
            emit consistCleared();
            return;
        }

        ConsistGroup g;
        g.module = v->module;
        g.configDir = v->configDir;
        g.config = v->config;
        g.count = 1;
        g.forward = true;

        // Одиночная единица ставится гружёной; коэффициенты правятся при редактировании состава
        g.payloadCoeff = 1.0;

        train.title = v->config;
        train.groups << g;
    }
    else
    {
        emit consistCleared();
        return;
    }

    const TrainSummary s = catalog_->summarize(train.groups);

    train.length = s.length;
    train.mass = s.mass;
    train.autopilotCapable = s.autopilotCapable;

    emit consistSelected(train);
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void RollingStockPanel::clearSelection()
{
    tree_->setCurrentItem(nullptr);
    tree_->clearSelection();
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
bool RollingStockPanel::selectTrain(const QString &name)
{
    for (int i = 0; i < tree_->topLevelItemCount(); ++i)
    {
        QTreeWidgetItem *root = tree_->topLevelItem(i);

        for (int j = 0; j < root->childCount(); ++j)
        {
            QTreeWidgetItem *it = root->child(j);

            if (it->data(ColName, ROLE_KIND).toInt() == KindTrain && it->text(ColName) == name)
            {
                // Составы показываются на вкладке «Готовые составы»
                if (tab_ != 1)
                {
                    setActiveTab(1);
                }

                tree_->setCurrentItem(it);
                return true;
            }
        }
    }

    return false;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
bool RollingStockPanel::selectVehicle(const QString &config_dir, const QString &config)
{
    for (int i = 0; i < tree_->topLevelItemCount(); ++i)
    {
        QTreeWidgetItem *root = tree_->topLevelItem(i);

        for (int j = 0; j < root->childCount(); ++j)
        {
            QTreeWidgetItem *dir = root->child(j);

            for (int k = 0; k < dir->childCount(); ++k)
            {
                QTreeWidgetItem *it = dir->child(k);

                if (it->data(ColName, ROLE_KIND).toInt() == KindVehicle
                    && it->data(ColName, ROLE_CONFIG_DIR).toString() == config_dir
                    && it->text(ColName) == config)
                {
                    tree_->setCurrentItem(it);
                    return true;
                }
            }
        }
    }

    return false;
}

} // namespace rse
