#ifndef     RSE_VEHICLE_CATALOG_H
#define     RSE_VEHICLE_CATALOG_H

#include    "catalog/catalog-data.h"

#include    <QMap>
#include    <QSet>
#include    <QString>
#include    <QStringList>
#include    <QVector>

namespace rse
{

//------------------------------------------------------------------------------
// Каталог подвижного состава установленной игры RRS:
// единицы (cfg/vehicles), готовые составы (cfg/trains) и модули (modules)
//------------------------------------------------------------------------------
class VehicleCatalog
{
public:

    /// Каталог игры по каталогу маршрута: routes/<имя> -> родитель каталога routes
    static QString gameRootForRoute(const QString &route_dir);

    /// Похож ли каталог на корень игры (есть cfg/vehicles)
    static bool isGameRoot(const QString &dir);

    /// Загрузить каталог из корня игры. Ручные привязки к модулям
    /// (setManualModule, loadManualModules) сохраняются между загрузками
    bool load(const QString &game_root);

    /// Сбросить загруженные данные (ручные привязки остаются)
    void clear();

    bool isLoaded() const
    {
        return loaded_;
    }

    const QString &gameRoot() const
    {
        return game_root_;
    }

    const QString &errorString() const
    {
        return error_;
    }

    /// Некритичные замечания, найденные при загрузке
    const QStringList &warnings() const
    {
        return warnings_;
    }

    const QVector<VehicleConfigData> &vehicles() const
    {
        return vehicles_;
    }

    const QVector<TrainConfigData> &trains() const
    {
        return trains_;
    }

    const VehicleConfigData *findVehicle(const QString &config_dir,
                                         const QString &config) const;

    const TrainConfigData *findTrain(const QString &name) const;

    /// Есть ли в игре каталог modules (в исходниках без сборки его нет)
    bool hasModulesDir() const
    {
        return has_modules_dir_;
    }

    /// Найденные в modules библиотеки в виде "каталог/модуль"
    const QStringList &installedModules() const
    {
        return installed_modules_;
    }

    /// Число единиц, для которых модуль неизвестен
    int unresolvedCount() const;

    /// Допустимое имя файла состава (cfg/trains/<имя>.xml)
    static bool isValidTrainName(const QString &name);

    /// Путь к файлу состава в каталоге игры
    QString trainFilePath(const QString &name) const;

    /// Ошибки, из-за которых состав нельзя записать (пусто - можно)
    QStringList validateTrain(const TrainConfigData &train) const;

    /// Записать состав в cfg/trains/<имя>.xml. Существующий файл заменяется только
    /// при overwrite. Каталог в памяти не меняется - после записи вызовите load()
    bool saveTrain(const TrainConfigData &train, bool overwrite, QString *error = nullptr) const;

    /// Длина, масса и признаки состава, заданного группами
    TrainSummary summarize(const QVector<ConsistGroup> &groups) const;

    TrainSummary summarize(const TrainConfigData &train) const
    {
        return summarize(train.groups);
    }

    //--------------------------------------------------------------------------
    // Ручные привязки каталога конфигов к модулю
    //--------------------------------------------------------------------------

    /// Назначить модуль каталогу конфигов. Пустая ссылка удаляет привязку.
    /// Привязки единиц пересчитываются сразу
    void setManualModule(const QString &config_dir, const ModuleRef &module);

    const QMap<QString, ModuleRef> &manualModules() const
    {
        return manual_modules_;
    }

    /// Прочитать ручные привязки из JSON-файла (нет файла - не ошибка)
    bool loadManualModules(const QString &path);

    /// Записать ручные привязки в JSON-файл
    bool saveManualModules(const QString &path) const;

private:

    bool loaded_ = false;
    QString game_root_;
    QString error_;

    /// Замечания чтения файлов (не зависят от ручных привязок)
    QStringList load_warnings_;

    /// Все замечания: чтение файлов плюс замечания по привязке к модулям
    QStringList warnings_;

    QVector<VehicleConfigData> vehicles_;
    QVector<TrainConfigData> trains_;

    bool has_modules_dir_ = false;
    QStringList installed_modules_;

    /// Имена установленных модулей в виде "каталог/файл" и "каталог/файл без lib"
    QSet<QString> installed_lookup_;

    QMap<QString, ModuleRef> manual_modules_;

    /// Модуль, указанный для единицы в составах, и число таких указаний
    struct ModuleCandidate
    {
        ModuleRef ref;
        int uses = 0;
    };

    /// Кандидаты по единице ("каталог/конфиг") и по каталогу конфигов
    QMap<QString, QVector<ModuleCandidate>> exact_candidates_;
    QMap<QString, QVector<ModuleCandidate>> dir_candidates_;

    static void addCandidate(QVector<ModuleCandidate> &list, const ModuleRef &ref);

    void loadVehicles();
    void loadTrains();
    void scanInstalledModules();

    /// Определить модуль каждой единицы и проверить его наличие в modules
    void resolveModules();
};

} // namespace rse

#endif // RSE_VEHICLE_CATALOG_H
