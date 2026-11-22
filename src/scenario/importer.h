#ifndef     RSE_IMPORTER_H
#define     RSE_IMPORTER_H

#include    "catalog/vehicle-catalog.h"
#include    "scenario/project.h"

#include    <QString>
#include    <QStringList>

namespace rse
{

//------------------------------------------------------------------------------
// Результат импорта существующего сценария
//------------------------------------------------------------------------------
struct ImportResult
{
    bool ok = false;
    QString error;

    ProjectInfo info;
    Scenario scenario;

    /// Что распознано и что сохранено как есть, замечания
    QStringList notes;
    QStringList warnings;

    int trainsRecognized = 0;
    int eventsRecognized = 0;
    int opaqueBlocks = 0;
};

/// Прочитать сценарий из каталога routes/<маршрут>/scenarios/<имя>: main.lua,
/// timetable/*.xml, README.md. Всё, что редактор не понимает (функции Lua,
/// нестандартные триггеры, поезда с неизвестным составом), сохраняется как есть
ImportResult importScenario(const Route &route, const VehicleCatalog &catalog,
                            const QString &scenario_dir);

/// Разбор текста main.lua (для тестов): без чтения расписаний и README
ImportResult importLua(const Route &route, const VehicleCatalog &catalog, const QString &lua_text);

} // namespace rse

#endif // RSE_IMPORTER_H
