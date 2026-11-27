#ifndef     RSE_EXPORT_H
#define     RSE_EXPORT_H

#include    "catalog/vehicle-catalog.h"
#include    "scenario/project.h"

#include    <QString>
#include    <QStringList>
#include    <QVector>

namespace rse
{

//------------------------------------------------------------------------------
// Параметры экспорта
//------------------------------------------------------------------------------
struct ExportOptions
{
    ProjectInfo info;

    /// Заменить существующий каталог сценария (файлы main.lua, README.md, timetable)
    bool overwrite = false;

    /// Дополнительно генерировать события для поездов с автоведением и
    /// согласовывать их маршруты по категории; для поездов без автоведения
    /// события по графику создаются всегда
    bool autoTriggers = true;

    /// Каталог для «пакета-дополнения» (пусто - не собирать; этап 29)
    QString addonDir;
};

//------------------------------------------------------------------------------
// Что будет записано для одного поезда
//------------------------------------------------------------------------------
struct ExportTrain
{
    int index = 0;

    /// Имя переменной Lua
    QString luaVar;

    /// Имя конфига в cfg/trains
    QString configName;

    /// Конфиг надо создать (иначе берётся существующий)
    bool writeConfig = false;
    TrainConfigData config;

    bool autoMode = false;
};

//------------------------------------------------------------------------------
// План экспорта: проверки и список файлов
//------------------------------------------------------------------------------
struct ExportPlan
{
    QStringList errors;
    QStringList warnings;

    /// main.lua в каталоге сценария изменён со времени прошлого экспорта
    /// (правки вручную будут потеряны при записи)
    bool mainEdited = false;

    /// Хеш существующего main.lua (пусто - файла нет)
    QString existingMainSha256;

    /// Имя резервной копии существующего main.lua при перезаписи (пусто - файла нет;
    /// копия создаётся, только если новая версия отличается от старой)
    QString mainBackupName;

    QVector<ExportTrain> trains;

    /// События, созданные редактором для продолжений: переименование поезда после сцепки
    QVector<ScenarioEvent> formationEvents;

    /// Триггеры по расписаниям (маршруты, очерёдность по категории поезда)
    QVector<ScenarioEvent> autoEvents;

    /// Каталог сценария
    QString scenarioDir;

    /// Время начала игры, с
    int startTime = 12 * 3600;

    /// Файлы, которые будут записаны (для показа пользователю)
    QStringList files;

    bool ok() const
    {
        return errors.isEmpty();
    }
};

/// Предупреждения экспорта, разложенные по важности: stop - поезд может не поехать,
/// jam - возможные заторы и ожидания, info - остальное
struct ExportWarningGroups
{
    QStringList stop;
    QStringList jam;
    QStringList info;
};

ExportWarningGroups groupExportWarnings(const QStringList &warnings);

/// Проверить сценарий и подготовить экспорт, ничего не записывая
ExportPlan planExport(const Route &route, const VehicleCatalog &catalog, const Scenario &scenario,
                      const ExportOptions &options);

/// Текст main.lua
QString luaMain(const Scenario &scenario, const ExportPlan &plan, const ProjectInfo &info);

/// Текст timetable/<поезд>.xml. approach/removal - траектории подхода и ухода для каждой
/// станции расписания (по порядку; пусто - не задавать)
QByteArray timetableXml(const PlacedTrain &train, const QStringList &approach,
                        const QStringList &removal);

/// Текст README.md
QString readmeText(const Scenario &scenario, const ExportPlan &plan, const ProjectInfo &info,
                   const Route &route);

/// Траектории подхода и ухода станций расписания: ближайшие траектории пути
/// перед и после траектории станции
void timetableApproachRemoval(const Route &route, const PlacedTrain &train,
                              QStringList *approach, QStringList *removal);

/// Записать сценарий (и новые составы в cfg/trains) по плану. Возвращает false и
/// причину в error. Если options.addonDir не пуст, после записи собирается
/// «пакет-дополнение» (этап 29)
bool writeExport(const Route &route, const VehicleCatalog &catalog, const Scenario &scenario,
                 const ExportPlan &plan, const ExportOptions &options, QString *error = nullptr);

/// Собрать «пакет-дополнение»: минимальное дерево игры, которое копируется поверх
/// установленной поверху - routes/<маршрут>/scenarios/<имя>/ (main.lua, README.md,
/// timetable/) и cfg/trains/<составы из плана>.xml; в корне пакета -
/// README-package.md с инструкцией. Каталог создаётся при необходимости, прежний
/// пакет того же сценария заменяется
bool buildAddonPackage(const Route &route, const Scenario &scenario,
                       const VehicleCatalog &catalog, const ExportPlan &plan,
                       const QString &scenario_dir, const QString &addon_dir,
                       QString *error = nullptr);

/// Допустимое имя каталога сценария
bool isValidScenarioName(const QString &name);

} // namespace rse

#endif // RSE_EXPORT_H
