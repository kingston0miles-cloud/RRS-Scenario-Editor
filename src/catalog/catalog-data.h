#ifndef     RSE_CATALOG_DATA_H
#define     RSE_CATALOG_DATA_H

#include    <QPair>
#include    <QString>
#include    <QVector>

namespace rse
{

//------------------------------------------------------------------------------
// Ссылка на модуль (библиотеку) подвижного состава: modules/<moduleDir>/<module>
//------------------------------------------------------------------------------
struct ModuleRef
{
    QString moduleDir;
    QString module;

    bool isValid() const
    {
        return !module.isEmpty();
    }

    /// Строка вида "каталог/модуль"
    QString id() const
    {
        return moduleDir + QLatin1Char('/') + module;
    }

    bool operator==(const ModuleRef &other) const
    {
        return moduleDir == other.moduleDir && module == other.module;
    }

    bool operator!=(const ModuleRef &other) const
    {
        return !(*this == other);
    }
};

//------------------------------------------------------------------------------
// Откуда известна привязка конфига единицы к модулю
//------------------------------------------------------------------------------
enum class ModuleSource
{
    /// Привязка неизвестна
    Unknown,
    /// Эта единица прямо указана в одном из готовых составов (cfg/trains)
    TrainExact,
    /// В составах для этого каталога конфигов всегда указан один и тот же модуль
    TrainDir,
    /// Составы расходятся; выбран вариант, который встречается заметно чаще
    /// (при наличии каталога modules выбирается установленный вариант)
    TrainMajority,
    /// Назначена вручную
    Manual
};

//------------------------------------------------------------------------------
// Коэффициенты основного сопротивления (cfg/main-resist/<имя>.xml):
// w = b0 + (b1 + b2 v + b3 v^2) / q, кгс/т, v - км/ч, q - нагрузка на ось, т
// (если q0 > 1; иначе q = 1), как в Vehicle::mainResistCoeffs симулятора
//------------------------------------------------------------------------------
struct ResistCoeffs
{
    double b0 = 0.7;
    double b1 = 8.0;
    double b2 = 0.08;
    double b3 = 0.002;
    double q0 = 1.0;
};

//------------------------------------------------------------------------------
// Единица подвижного состава: главный конфиг cfg/vehicles/<configDir>/<config>.xml
//------------------------------------------------------------------------------
struct VehicleConfigData
{
    /// Каталог конфигов (ModuleConfigDir в конфиге состава)
    QString configDir;

    /// Имя конфига без расширения (ModuleConfig в конфиге состава)
    QString config;

    /// Длина, м (по умолчанию как в симуляторе)
    double length = 13.92;

    /// Масса порожней единицы, кг
    double emptyMass = 25000.0;

    /// Масса полной загрузки, кг
    double payloadMass = 65000.0;

    /// Есть кран машиниста (признак локомотива или моторвагонной единицы)
    bool isLocomotive = false;

    /// В конфиге задан модуль автопилота (возможно автоведение)
    bool hasAutopilot = false;

    /// Число осей (NumAxis) и основное сопротивление (MainResist)
    int numAxis = 4;
    QString mainResist;
    ResistCoeffs resist;

    /// Конструкционная скорость из конфига автопилота (V_constr), км/ч; 0 - неизвестна
    double constructionSpeed = 0.0;

    /// Модуль, который загружает эту единицу
    ModuleRef module;
    ModuleSource moduleSource = ModuleSource::Unknown;

    /// Уникальный идентификатор "каталог/конфиг"
    QString id() const
    {
        return configDir + QLatin1Char('/') + config;
    }
};

//------------------------------------------------------------------------------
// Группа одинаковых единиц в конфиге состава (секция Vehicle)
//------------------------------------------------------------------------------
struct ConsistGroup
{
    ModuleRef module;
    QString configDir;
    QString config;

    /// Число единиц в группе
    int count = 1;

    /// Ориентация группы вдоль состава (IsOrientationForward)
    bool forward = true;

    /// Коэффициент загрузки 0...1 (PayloadCoeff)
    double payloadCoeff = 0.0;

    /// Тормозные башмаки (IsBrakeShoes)
    bool brakeShoes = false;

    QString configId() const
    {
        return configDir + QLatin1Char('/') + config;
    }
};

//------------------------------------------------------------------------------
// Готовый состав: cfg/trains/<name>.xml
//------------------------------------------------------------------------------
struct TrainConfigData
{
    /// Имя файла без расширения - его указывают в сценарии (TrainData.config)
    QString name;

    QString title;
    QString description;

    /// Зарядное давление, МПа
    double chargingPressure = 0.5;

    /// Начальное давление в главных резервуарах, МПа
    double initMainResPressure = 0.9;

    /// Состав без воздуха
    bool noAir = false;

    QVector<ConsistGroup> groups;

    /// Прочие поля секции Common (ClientName, TrainID, …): сохраняются как есть
    QVector<QPair<QString, QString>> extraCommon;
};

//------------------------------------------------------------------------------
// Итоги по составу, посчитанные по каталогу
//------------------------------------------------------------------------------
struct TrainSummary
{
    int vehicles = 0;
    int locomotives = 0;

    /// Длина, м
    double length = 0.0;

    /// Масса с учётом загрузки, кг
    double mass = 0.0;

    /// Все единицы состава найдены в каталоге (иначе длина и масса неполные)
    bool complete = true;

    /// Крайняя единица состава (первая или последняя) имеет автопилот.
    /// Только в этом случае флаг auto в сценарии включит автоведение
    bool autopilotCapable = false;
};

} // namespace rse

#endif // RSE_CATALOG_DATA_H
