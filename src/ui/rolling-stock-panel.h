#ifndef     RSE_ROLLING_STOCK_PANEL_H
#define     RSE_ROLLING_STOCK_PANEL_H

#include    <QWidget>

#include    "catalog/vehicle-catalog.h"
#include    "scenario/scenario.h"

class QLabel;
class QLineEdit;
class QTreeWidget;
class QTreeWidgetItem;

namespace rse
{

//------------------------------------------------------------------------------
// Панель «Подвижной состав»: единицы игры (по каталогам конфигов) и готовые
// составы. Показывает длину, массу, тип и откуда известен модуль единицы.
// Ручное назначение модуля - в контекстном меню единицы.
//------------------------------------------------------------------------------
class RollingStockPanel : public QWidget
{
    Q_OBJECT

public:

    explicit RollingStockPanel(QWidget *parent = nullptr);

    /// Показать каталог (панель им не владеет). nullptr - очистить
    void setCatalog(VehicleCatalog *catalog);

    /// Обновить содержимое после изменения каталога
    void refresh();

    /// Применить текст поиска (регистр не учитывается)
    void setFilter(const QString &text);

    /// Снять выбор строки
    void clearSelection();

    /// Выбрать готовый состав по имени (для проверок и программного выбора);
    /// false - такого нет в списке
    bool selectTrain(const QString &name);

    /// Выбрать единицу по каталогу конфигов и имени конфига
    bool selectVehicle(const QString &config_dir, const QString &config);

    /// Число видимых строк-единиц и строк-составов (для проверок)
    int visibleVehicleCount() const;
    int visibleTrainCount() const;

signals:

    /// Пользователь назначил модуль каталогу конфигов
    void manualModuleChanged();

    /// Запрошено создание состава; для пустых строк - с нуля, иначе с этой единицы
    void newTrainRequested(const QString &config_dir, const QString &config);

    /// Запрошена правка готового состава (as_copy - сохранить под новым именем)
    void editTrainRequested(const QString &name, bool as_copy);

    /// Выбран состав или единица для расстановки на карте
    void consistSelected(const rse::PlacedTrain &train);

    /// Выбор снят или выбрана строка, которую ставить нельзя (заголовок, каталог)
    void consistCleared();

private slots:

    void onContextMenu(const QPoint &pos);
    void onCurrentItemChanged(QTreeWidgetItem *item);

private:

    VehicleCatalog *catalog_ = nullptr;

    QLineEdit *filter_edit_ = nullptr;
    QTreeWidget *tree_ = nullptr;
    QLabel *summary_label_ = nullptr;

    void fillVehicles();
    void fillTrains();
    void applyFilter();

    static QString sourceText(ModuleSource source);
};

} // namespace rse

#endif // RSE_ROLLING_STOCK_PANEL_H
