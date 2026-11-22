#ifndef     RSE_PROJECT_H
#define     RSE_PROJECT_H

#include    "scenario/events.h"
#include    "scenario/scenario.h"

#include    <QString>

namespace rse
{

//------------------------------------------------------------------------------
// Сведения о сценарии, не относящиеся к расстановке поездов
//------------------------------------------------------------------------------
struct ProjectInfo
{
    /// Имя каталога сценария (routes/<маршрут>/scenarios/<имя>)
    QString scenarioName;

    /// Описание (текст README.md)
    QString description;

    /// Время начала игры, с от начала суток; отрицательное - выбрать автоматически
    int startTime = -1;

    /// События (триггеры и действия)
    QVector<ScenarioEvent> events;

    /// Код Lua, не распознанный при импорте: сохраняется как есть. pre - объявления
    /// (функции, константы), пишутся до поездов; post - остальное, после событий
    QStringList opaquePre;
    QStringList opaquePost;

    /// Каталог маршрута и корень игры (для повторного открытия)
    QString routeDir;
    QString gameRoot;
};

/// Время начала игры по умолчанию: за 3 мин до первого отправления по графику,
/// без графиков - 12:00
int suggestStartTime(const Scenario &scenario);

/// Сохранить проект в файл (JSON). Пути к маршруту и игре записываются
/// относительно файла проекта, если возможно, и абсолютными
bool saveProject(const QString &path, const ProjectInfo &info, const Scenario &scenario,
                 QString *error = nullptr);

/// Прочитать проект. Длина, масса и признак автопилота поездов берутся из файла
/// (при необходимости их пересчитывают по каталогу)
bool loadProject(const QString &path, ProjectInfo *info, Scenario *scenario,
                 QString *error = nullptr);

/// Каталог маршрута из файла проекта, не загружая поезда (найденный
/// относительный или абсолютный путь; пусто - не найден)
QString projectRouteDir(const QString &path);

} // namespace rse

#endif // RSE_PROJECT_H
