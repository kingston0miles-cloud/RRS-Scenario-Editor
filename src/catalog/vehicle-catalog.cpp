#include    "catalog/vehicle-catalog.h"

#include    <QDir>
#include    <QFile>
#include    <QFileInfo>
#include    <QJsonDocument>
#include    <QJsonObject>
#include    <QStringDecoder>
#include    <QXmlStreamReader>
#include    <QXmlStreamWriter>
#include    <QRegularExpression>

#include    <algorithm>

namespace rse
{

namespace
{

using Fields = QMap<QString, QString>;

/// Секция конфига: прямой потомок корневого элемента с листовыми полями
struct Section
{
    QString name;
    Fields fields;
};

//------------------------------------------------------------------------------
// Прочитать все секции XML-конфига симулятора (<Config><Секция><Поле>текст).
// Файл читается терпимо: байты не в UTF-8 (например, комментарий в cp1251)
// заменяются, о чём сообщает invalid_utf8, а разбор продолжается
//------------------------------------------------------------------------------
bool readSections(const QString &path, QVector<Section> &out, QString *error,
                  bool *invalid_utf8 = nullptr)
{
    QFile file(path);

    if (!file.open(QIODevice::ReadOnly))
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("не удалось открыть файл");
        }

        return false;
    }

    QStringDecoder decoder(QStringDecoder::Utf8);
    QString text = decoder(file.readAll());

    if (invalid_utf8 != nullptr)
    {
        *invalid_utf8 = decoder.hasError();
    }

    if (text.startsWith(QChar(0xFEFF)))
    {
        text.remove(0, 1);
    }

    // Из строки кодировка в объявлении XML игнорируется
    QXmlStreamReader xml(text);

    // 0 - вне корня, 1 - внутри корня, 2 - внутри секции
    int depth = 0;
    Section cur;

    while (!xml.atEnd())
    {
        xml.readNext();

        if (xml.isStartElement())
        {
            const QString name = xml.name().toString();

            if (depth == 0)
            {
                depth = 1;
            }
            else if (depth == 1)
            {
                depth = 2;
                cur = Section();
                cur.name = name;
            }
            else if (depth == 2)
            {
                // Чтение потомка забирает и его закрывающий тег
                cur.fields.insert(name, xml.readElementText(
                    QXmlStreamReader::SkipChildElements).trimmed());
            }
        }
        else if (xml.isEndElement())
        {
            if (depth == 2)
            {
                out.push_back(cur);
                depth = 1;
            }
            else if (depth == 1)
            {
                depth = 0;
            }
        }
    }

    if (xml.hasError())
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("%1 (строка %2)")
                         .arg(xml.errorString()).arg(xml.lineNumber());
        }

        return false;
    }

    return true;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
double toDouble(const QString &s, double def)
{
    bool ok = false;
    const double v = s.toDouble(&ok);

    return ok ? v : def;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
int toInt(const QString &s, int def)
{
    bool ok = false;
    const int v = s.toInt(&ok);

    return ok ? v : def;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
bool toBool(const QString &s, bool def)
{
    const QString v = s.trimmed().toLower();

    if (v.isEmpty())
    {
        return def;
    }

    if (v == QLatin1String("true") || v == QLatin1String("yes"))
    {
        return true;
    }

    if (v == QLatin1String("false") || v == QLatin1String("no"))
    {
        return false;
    }

    return toInt(v, def ? 1 : 0) != 0;
}

//------------------------------------------------------------------------------
// Имя библиотеки без префикса lib
//------------------------------------------------------------------------------
QString stripLibPrefix(const QString &name)
{
    if (name.size() > 3 && name.startsWith(QLatin1String("lib")))
    {
        return name.mid(3);
    }

    return name;
}

} // namespace

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
QString VehicleCatalog::gameRootForRoute(const QString &route_dir)
{
    // Строковые операции, а не QDir::cdUp: cdUp не работает для несуществующих путей
    const QString clean = QDir::cleanPath(QDir(route_dir).absolutePath());

    // routes/<имя маршрута> -> routes -> корень игры
    const QString routes = QFileInfo(clean).absolutePath();

    if (routes.isEmpty() || routes == clean)
    {
        return QString();
    }

    const QString root = QFileInfo(routes).absolutePath();

    return (root == routes) ? QString() : root;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
bool VehicleCatalog::isGameRoot(const QString &dir)
{
    return !dir.isEmpty() && QDir(dir).exists(QStringLiteral("cfg/vehicles"));
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void VehicleCatalog::clear()
{
    loaded_ = false;
    game_root_.clear();
    error_.clear();
    load_warnings_.clear();
    warnings_.clear();
    vehicles_.clear();
    trains_.clear();
    has_modules_dir_ = false;
    installed_modules_.clear();
    installed_lookup_.clear();
    exact_candidates_.clear();
    dir_candidates_.clear();
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
bool VehicleCatalog::load(const QString &game_root)
{
    const QString root = QDir(game_root).absolutePath();

    // Ошибочный выбор каталога не должен стирать уже загруженный каталог:
    // проверяем до очистки, прежние данные остаются
    if (!isGameRoot(root))
    {
        error_ = QStringLiteral("В каталоге нет cfg/vehicles. Выберите корневой каталог игры "
                                "(рядом с папками cfg, routes, modules): %1").arg(root);
        return false;
    }

    clear();

    game_root_ = root;

    loadVehicles();
    loadTrains();
    scanInstalledModules();

    // Единицы, на которые ссылаются составы, но которых нет в cfg/vehicles
    for (const TrainConfigData &train : std::as_const(trains_))
    {
        for (const ConsistGroup &g : train.groups)
        {
            if (findVehicle(g.configDir, g.config) == nullptr)
            {
                load_warnings_ << QStringLiteral("Состав %1: единица %2 не найдена в cfg/vehicles")
                                      .arg(train.name, g.configId());
            }
        }
    }

    if (vehicles_.isEmpty())
    {
        load_warnings_ << QStringLiteral("В cfg/vehicles не найдено ни одной единицы подвижного состава");
    }

    loaded_ = true;
    resolveModules();

    return true;
}

//------------------------------------------------------------------------------
// cfg/vehicles/<каталог>/<конфиг>.xml. Единица - файл, у которого секция Vehicle
// содержит Length. Остальные файлы (тормоза, шланги, контроллеры) пропускаются
//------------------------------------------------------------------------------
void VehicleCatalog::loadVehicles()
{
    const QDir vehicles_dir(game_root_ + QStringLiteral("/cfg/vehicles"));

    const QFileInfoList dirs = vehicles_dir.entryInfoList(
        QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);

    for (const QFileInfo &d : dirs)
    {
        const QDir sub(d.absoluteFilePath());

        const QFileInfoList files = sub.entryInfoList(
            QStringList() << QStringLiteral("*.xml"), QDir::Files, QDir::Name);

        for (const QFileInfo &f : files)
        {
            QVector<Section> sections;
            QString err;
            bool bad_utf8 = false;

            if (!readSections(f.absoluteFilePath(), sections, &err, &bad_utf8))
            {
                // Вспомогательные файлы (приборы, контроллеры) единицами не являются.
                // Сообщаем о неразобранном файле, только если он похож на единицу
                QFile raw(f.absoluteFilePath());

                if (raw.open(QIODevice::ReadOnly)
                    && raw.readAll().contains("<Length>"))
                {
                    load_warnings_ << QStringLiteral("cfg/vehicles/%1/%2: %3")
                                          .arg(d.fileName(), f.fileName(), err);
                }

                continue;
            }

            if (bad_utf8)
            {
                load_warnings_ << QStringLiteral("cfg/vehicles/%1/%2: файл не в кодировке UTF-8, "
                                                 "повреждённые символы заменены")
                                      .arg(d.fileName(), f.fileName());
            }

            const Section *veh = nullptr;

            for (const Section &s : std::as_const(sections))
            {
                if (s.name == QLatin1String("Vehicle") && s.fields.contains(QStringLiteral("Length")))
                {
                    veh = &s;
                    break;
                }
            }

            if (veh == nullptr)
            {
                continue;
            }

            const Fields &fl = veh->fields;

            VehicleConfigData v;
            v.configDir = d.fileName();
            v.config = f.completeBaseName();
            v.length = toDouble(fl.value(QStringLiteral("Length")), v.length);
            v.emptyMass = toDouble(fl.value(QStringLiteral("EmptyMass")), v.emptyMass);
            v.payloadMass = toDouble(fl.value(QStringLiteral("PayloadMass")), v.payloadMass);

            v.isLocomotive = !fl.value(QStringLiteral("BrakeCraneModule")).isEmpty()
                          || !fl.value(QStringLiteral("LocoCraneModule")).isEmpty();

            v.hasAutopilot = !fl.value(QStringLiteral("AutopilotModule")).isEmpty();

            v.numAxis = std::max(1, toInt(fl.value(QStringLiteral("NumAxis")), v.numAxis));
            v.mainResist = fl.value(QStringLiteral("MainResist"));

            if (!v.mainResist.isEmpty())
            {
                QVector<Section> rs;
                QString rerr;
                bool rbad = false;

                if (readSections(game_root_ + QStringLiteral("/cfg/main-resist/%1.xml").arg(v.mainResist),
                                 rs, &rerr, &rbad))
                {
                    for (const Section &sec : std::as_const(rs))
                    {
                        if (sec.name == QLatin1String("MainResist"))
                        {
                            v.resist.b0 = toDouble(sec.fields.value(QStringLiteral("b0")), v.resist.b0);
                            v.resist.b1 = toDouble(sec.fields.value(QStringLiteral("b1")), v.resist.b1);
                            v.resist.b2 = toDouble(sec.fields.value(QStringLiteral("b2")), v.resist.b2);
                            v.resist.b3 = toDouble(sec.fields.value(QStringLiteral("b3")), v.resist.b3);
                            v.resist.q0 = toDouble(sec.fields.value(QStringLiteral("q0")), v.resist.q0);
                        }
                    }
                }
                else
                {
                    load_warnings_ << QStringLiteral("cfg/vehicles/%1/%2: не найден cfg/main-resist/%3.xml, "
                                                     "сопротивление принято по умолчанию")
                                          .arg(d.fileName(), f.fileName(), v.mainResist);
                }
            }

            // Конструкционная скорость - из конфига автопилота этой единицы
            const QString ap = fl.value(QStringLiteral("AutopilotConfig"));

            if (!ap.isEmpty())
            {
                QVector<Section> as;
                QString aerr;
                bool abad = false;
                const QString custom = fl.value(QStringLiteral("CustomModulesDir"));

                const QStringList candidates = {
                    sub.filePath(ap + QStringLiteral(".xml")),
                    vehicles_dir.filePath(custom + QStringLiteral("/") + ap + QStringLiteral(".xml")) };

                for (const QString &path : candidates)
                {
                    if (readSections(path, as, &aerr, &abad))
                    {
                        for (const Section &sec : std::as_const(as))
                        {
                            if (sec.fields.contains(QStringLiteral("V_constr")))
                            {
                                v.constructionSpeed = toDouble(sec.fields.value(QStringLiteral("V_constr")), 0.0);
                            }
                        }

                        break;
                    }
                }
            }

            vehicles_.push_back(v);
        }
    }
}

//------------------------------------------------------------------------------
// cfg/trains/*.xml. Значения по умолчанию такие же, как в Train::loadTrain
//------------------------------------------------------------------------------
void VehicleCatalog::loadTrains()
{
    const QDir trains_dir(game_root_ + QStringLiteral("/cfg/trains"));

    const QFileInfoList files = trains_dir.entryInfoList(
        QStringList() << QStringLiteral("*.xml"), QDir::Files, QDir::Name);

    for (const QFileInfo &f : files)
    {
        QVector<Section> sections;
        QString err;
        bool bad_utf8 = false;

        if (!readSections(f.absoluteFilePath(), sections, &err, &bad_utf8))
        {
            load_warnings_ << QStringLiteral("cfg/trains/%1: %2").arg(f.fileName(), err);
            continue;
        }

        if (bad_utf8)
        {
            load_warnings_ << QStringLiteral("cfg/trains/%1: файл не в кодировке UTF-8, "
                                             "повреждённые символы заменены (симулятор требует UTF-8)")
                                  .arg(f.fileName());
        }

        TrainConfigData t;
        t.name = f.completeBaseName();

        for (const Section &s : std::as_const(sections))
        {
            const Fields &fl = s.fields;

            if (s.name == QLatin1String("Common"))
            {
                t.title = fl.value(QStringLiteral("Title"));
                t.description = fl.value(QStringLiteral("Description"));
                t.chargingPressure = toDouble(fl.value(QStringLiteral("ChargingPressure")), t.chargingPressure);
                t.initMainResPressure = toDouble(fl.value(QStringLiteral("InitMainResPressure")), t.initMainResPressure);
                t.noAir = toBool(fl.value(QStringLiteral("NoAir")), false);

                static const QStringList known = {
                    QStringLiteral("Title"), QStringLiteral("Description"),
                    QStringLiteral("ChargingPressure"), QStringLiteral("InitMainResPressure"),
                    QStringLiteral("NoAir") };

                for (auto it = fl.constBegin(); it != fl.constEnd(); ++it)
                {
                    if (!known.contains(it.key()))
                    {
                        t.extraCommon.push_back({ it.key(), it.value() });
                    }
                }

                continue;
            }

            if (s.name != QLatin1String("Vehicle"))
            {
                continue;
            }

            ConsistGroup g;
            g.module.module = fl.value(QStringLiteral("Module"));
            g.config = fl.value(QStringLiteral("ModuleConfig"));

            if (g.module.module.isEmpty() || g.config.isEmpty())
            {
                load_warnings_ << QStringLiteral("Состав %1: у секции Vehicle не заданы "
                                                 "Module или ModuleConfig, она пропущена").arg(t.name);
                continue;
            }

            // Если каталоги не указаны, симулятор берёт одноимённые
            g.module.moduleDir = fl.value(QStringLiteral("ModuleDir"));
            g.configDir = fl.value(QStringLiteral("ModuleConfigDir"));

            if (g.module.moduleDir.isEmpty())
            {
                g.module.moduleDir = g.module.module;
            }

            if (g.configDir.isEmpty())
            {
                g.configDir = g.config;
            }

            g.count = toInt(fl.value(QStringLiteral("Count")), 0);
            g.forward = toBool(fl.value(QStringLiteral("IsOrientationForward")), true);
            g.payloadCoeff = toDouble(fl.value(QStringLiteral("PayloadCoeff")), 0.0);
            g.brakeShoes = toBool(fl.value(QStringLiteral("IsBrakeShoes")), false);

            t.groups.push_back(g);
        }

        if (t.title.isEmpty())
        {
            t.title = t.name;
        }

        trains_.push_back(t);
    }

    // Привязки к модулям, которые можно вывести из составов: все варианты и число
    // указаний каждого. Выбор между ними делает resolveModules()
    for (const TrainConfigData &t : std::as_const(trains_))
    {
        for (const ConsistGroup &g : t.groups)
        {
            addCandidate(exact_candidates_[g.configId()], g.module);
            addCandidate(dir_candidates_[g.configDir], g.module);
        }
    }
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void VehicleCatalog::addCandidate(QVector<ModuleCandidate> &list, const ModuleRef &ref)
{
    for (ModuleCandidate &c : list)
    {
        if (c.ref == ref)
        {
            ++c.uses;
            return;
        }
    }

    list.push_back({ ref, 1 });
}

//------------------------------------------------------------------------------
// modules/<каталог>/<библиотека>.(dll|so|dylib)
//------------------------------------------------------------------------------
void VehicleCatalog::scanInstalledModules()
{
    const QDir modules_dir(game_root_ + QStringLiteral("/modules"));

    has_modules_dir_ = modules_dir.exists();

    if (!has_modules_dir_)
    {
        return;
    }

    const QFileInfoList dirs = modules_dir.entryInfoList(
        QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);

    for (const QFileInfo &d : dirs)
    {
        const QDir sub(d.absoluteFilePath());

        for (const QFileInfo &f : sub.entryInfoList(QDir::Files, QDir::Name))
        {
            const QString suffix = f.suffix().toLower();

            if (suffix != QLatin1String("dll") && suffix != QLatin1String("so")
                && suffix != QLatin1String("dylib"))
            {
                continue;
            }

            const QString raw = f.completeBaseName();
            const QString name = stripLibPrefix(raw);

            installed_modules_ << d.fileName() + QLatin1Char('/') + name;
            installed_lookup_.insert(d.fileName() + QLatin1Char('/') + raw);
            installed_lookup_.insert(d.fileName() + QLatin1Char('/') + name);
        }
    }

    installed_modules_.removeDuplicates();
    installed_modules_.sort();
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void VehicleCatalog::resolveModules()
{
    warnings_ = load_warnings_;

    const bool check_installed = has_modules_dir_ && !installed_lookup_.isEmpty();

    enum class Pick
    {
        None,       ///< вариантов нет
        Single,     ///< вариант один (или единственный установленный)
        Majority,   ///< варианты расходятся, один строго чаще остальных
        Ambiguous   ///< выбрать нельзя
    };

    // Выбор модуля среди вариантов, указанных в составах. Если в игре есть каталог
    // modules, решает он: симулятор всё равно загрузит только установленный модуль
    auto pick = [&](const QVector<ModuleCandidate> &list, ModuleRef &out) -> Pick
    {
        if (list.isEmpty())
        {
            return Pick::None;
        }

        if (list.size() == 1)
        {
            out = list.first().ref;
            return Pick::Single;
        }

        QVector<ModuleCandidate> pool = list;

        if (check_installed)
        {
            QVector<ModuleCandidate> installed;

            for (const ModuleCandidate &c : list)
            {
                if (installed_lookup_.contains(c.ref.id()))
                {
                    installed.push_back(c);
                }
            }

            if (installed.size() == 1)
            {
                out = installed.first().ref;
                return Pick::Single;
            }

            if (!installed.isEmpty())
            {
                pool = installed;
            }
        }

        std::sort(pool.begin(), pool.end(), [](const ModuleCandidate &a, const ModuleCandidate &b)
        {
            return a.uses > b.uses;
        });

        if (pool.first().uses > pool.at(1).uses)
        {
            out = pool.first().ref;
            return Pick::Majority;
        }

        return Pick::Ambiguous;
    };

    // "модуль ×число указаний" через запятую, самые частые первыми
    auto describe = [](QVector<ModuleCandidate> list) -> QString
    {
        std::sort(list.begin(), list.end(), [](const ModuleCandidate &a, const ModuleCandidate &b)
        {
            return a.uses > b.uses;
        });

        QStringList parts;

        for (const ModuleCandidate &c : std::as_const(list))
        {
            parts << QStringLiteral("%1 ×%2").arg(c.ref.id()).arg(c.uses);
        }

        return parts.join(QStringLiteral(", "));
    };

    QSet<QString> warned;

    for (VehicleConfigData &v : vehicles_)
    {
        v.module = ModuleRef();
        v.moduleSource = ModuleSource::Unknown;

        auto man = manual_modules_.constFind(v.configDir);

        if (man != manual_modules_.constEnd())
        {
            v.module = man.value();
            v.moduleSource = ModuleSource::Manual;
            continue;
        }

        ModuleRef ref;
        const QVector<ModuleCandidate> exact = exact_candidates_.value(v.id());
        const Pick by_unit = pick(exact, ref);

        if (by_unit == Pick::Single || by_unit == Pick::Majority)
        {
            v.module = ref;
            v.moduleSource = (by_unit == Pick::Single) ? ModuleSource::TrainExact
                                                       : ModuleSource::TrainMajority;
        }

        if (exact.size() > 1 && !warned.contains(v.id()))
        {
            warned.insert(v.id());

            const QString outcome = (by_unit == Pick::Ambiguous)
                ? QStringLiteral("выбрать не удалось, назначьте модуль вручную")
                : QStringLiteral("выбран %1").arg(ref.id());

            warnings_ << QStringLiteral("Единица %1 указана в составах с разными модулями (%2): %3")
                             .arg(v.id(), describe(exact), outcome);
        }

        if (by_unit != Pick::None)
        {
            // Составы называют эту единицу прямо: каталог конфигов в целом не привлекаем
            continue;
        }

        // Единицы нет ни в одном составе: смотрим, какой модуль в составах
        // у остальных единиц того же каталога конфигов
        const QVector<ModuleCandidate> dir = dir_candidates_.value(v.configDir);
        const Pick by_dir = pick(dir, ref);

        if (by_dir == Pick::Single || by_dir == Pick::Majority)
        {
            v.module = ref;
            v.moduleSource = (by_dir == Pick::Single) ? ModuleSource::TrainDir
                                                      : ModuleSource::TrainMajority;
        }

        if (by_dir == Pick::Ambiguous && !warned.contains(v.configDir))
        {
            warned.insert(v.configDir);

            warnings_ << QStringLiteral("Каталог конфигов %1 используется в составах с разными "
                                        "модулями (%2): автоматическая привязка по каталогу "
                                        "невозможна, назначьте модуль вручную")
                             .arg(v.configDir, describe(dir));
        }
    }

    // Модуль, которого нет в modules, симулятор не загрузит
    if (check_installed)
    {
        QSet<QString> reported;

        for (const VehicleConfigData &v : std::as_const(vehicles_))
        {
            if (!v.module.isValid() || installed_lookup_.contains(v.module.id())
                || reported.contains(v.module.id()))
            {
                continue;
            }

            reported.insert(v.module.id());

            warnings_ << QStringLiteral("Модуль %1 (для %2) не найден в каталоге modules")
                             .arg(v.module.id(), v.id());
        }

        // Готовый состав со ссылкой на неустановленный модуль симулятор не загрузит
        for (const TrainConfigData &t : std::as_const(trains_))
        {
            QSet<QString> missing;

            for (const ConsistGroup &g : t.groups)
            {
                if (installed_lookup_.contains(g.module.id()) || missing.contains(g.module.id()))
                {
                    continue;
                }

                missing.insert(g.module.id());

                warnings_ << QStringLiteral("Состав %1: модуль %2 не найден в каталоге modules, "
                                            "симулятор такой состав не загрузит")
                                 .arg(t.name, g.module.id());
            }
        }
    }
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
const VehicleConfigData *VehicleCatalog::findVehicle(const QString &config_dir,
                                                     const QString &config) const
{
    for (const VehicleConfigData &v : vehicles_)
    {
        if (v.configDir == config_dir && v.config == config)
        {
            return &v;
        }
    }

    return nullptr;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
const TrainConfigData *VehicleCatalog::findTrain(const QString &name) const
{
    for (const TrainConfigData &t : trains_)
    {
        if (t.name == name)
        {
            return &t;
        }
    }

    return nullptr;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
int VehicleCatalog::unresolvedCount() const
{
    int n = 0;

    for (const VehicleConfigData &v : vehicles_)
    {
        if (!v.module.isValid())
        {
            ++n;
        }
    }

    return n;
}

//------------------------------------------------------------------------------
// Масса единицы - как в Vehicle::setPayloadCoeff: порожняя + груз * коэффициент
// (коэффициент ограничен диапазоном 0...1)
//------------------------------------------------------------------------------
TrainSummary VehicleCatalog::summarize(const QVector<ConsistGroup> &groups) const
{
    TrainSummary s;

    bool seen = false;
    const VehicleConfigData *first = nullptr;
    const VehicleConfigData *last = nullptr;

    for (const ConsistGroup &g : groups)
    {
        if (g.count <= 0)
        {
            continue;
        }

        const VehicleConfigData *v = findVehicle(g.configDir, g.config);

        s.vehicles += g.count;

        if (!seen)
        {
            first = v;
            seen = true;
        }

        last = v;

        if (v == nullptr)
        {
            s.complete = false;
            continue;
        }

        const double coeff = std::clamp(g.payloadCoeff, 0.0, 1.0);

        s.length += g.count * v->length;
        s.mass += g.count * (v->emptyMass + v->payloadMass * coeff);

        if (v->isLocomotive)
        {
            s.locomotives += g.count;
        }
    }

    // Автозапуск (флаг auto в сценарии) ищет автопилот у первой, затем у последней единицы
    s.autopilotCapable = (first != nullptr && first->hasAutopilot)
                      || (last != nullptr && last->hasAutopilot);

    return s;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void VehicleCatalog::setManualModule(const QString &config_dir, const ModuleRef &module)
{
    if (module.isValid())
    {
        ModuleRef m = module;

        if (m.moduleDir.isEmpty())
        {
            m.moduleDir = m.module;
        }

        manual_modules_.insert(config_dir, m);
    }
    else
    {
        manual_modules_.remove(config_dir);
    }

    if (loaded_)
    {
        resolveModules();
    }
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
bool VehicleCatalog::loadManualModules(const QString &path)
{
    manual_modules_.clear();

    QFile file(path);

    if (!file.exists())
    {
        if (loaded_)
        {
            resolveModules();
        }

        return true;
    }

    if (!file.open(QIODevice::ReadOnly))
    {
        return false;
    }

    QJsonParseError parse_error;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parse_error);

    if (parse_error.error != QJsonParseError::NoError || !doc.isObject())
    {
        return false;
    }

    const QJsonObject obj = doc.object();

    for (auto it = obj.begin(); it != obj.end(); ++it)
    {
        const QJsonObject o = it.value().toObject();

        ModuleRef m;
        m.module = o.value(QStringLiteral("module")).toString();
        m.moduleDir = o.value(QStringLiteral("moduleDir")).toString();

        if (m.moduleDir.isEmpty())
        {
            m.moduleDir = m.module;
        }

        if (m.isValid())
        {
            manual_modules_.insert(it.key(), m);
        }
    }

    if (loaded_)
    {
        resolveModules();
    }

    return true;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
bool VehicleCatalog::saveManualModules(const QString &path) const
{
    QJsonObject obj;

    for (auto it = manual_modules_.begin(); it != manual_modules_.end(); ++it)
    {
        QJsonObject o;
        o.insert(QStringLiteral("moduleDir"), it.value().moduleDir);
        o.insert(QStringLiteral("module"), it.value().module);

        obj.insert(it.key(), o);
    }

    QDir().mkpath(QFileInfo(path).absolutePath());

    QFile file(path);

    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        return false;
    }

    return file.write(QJsonDocument(obj).toJson(QJsonDocument::Indented)) >= 0;
}

} // namespace rse

namespace rse
{

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
bool VehicleCatalog::isValidTrainName(const QString &name)
{
    // Имя файла без расширения, которое симулятор подставит в cfg/trains/<имя>.xml
    static const QRegularExpression re(QStringLiteral("^[A-Za-z0-9А-Яа-яЁё_.+\\-]+$"));

    return re.match(name).hasMatch() && !name.startsWith(QLatin1Char('.'));
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
QString VehicleCatalog::trainFilePath(const QString &name) const
{
    return QDir(game_root_).filePath(QStringLiteral("cfg/trains/%1.xml").arg(name));
}

//------------------------------------------------------------------------------
// Проверить состав перед записью: имя, наличие единиц, известные модули
//------------------------------------------------------------------------------
QStringList VehicleCatalog::validateTrain(const TrainConfigData &train) const
{
    QStringList errors;

    if (!isValidTrainName(train.name))
    {
        errors << QStringLiteral("Имя файла состава «%1» не подходит: допустимы буквы, цифры, "
                                 "«_», «-», «.», «+».").arg(train.name);
    }

    int units = 0;

    for (const ConsistGroup &g : train.groups)
    {
        units += g.count;

        if (g.count <= 0)
        {
            continue;
        }

        if (!g.module.isValid())
        {
            errors << QStringLiteral("Для единицы %1 неизвестен модуль: назначьте его "
                                     "(правая кнопка по единице в списке).").arg(g.configId());
        }
    }

    if (units == 0)
    {
        errors << QStringLiteral("В составе нет ни одной единицы.");
    }

    return errors;
}

//------------------------------------------------------------------------------
// Записать cfg/trains/<имя>.xml в том виде, как их пишут вручную: Common и по
// секции Vehicle на группу. Запись через временный файл, чтобы не испортить
// существующий состав при сбое
//------------------------------------------------------------------------------
bool VehicleCatalog::saveTrain(const TrainConfigData &train, bool overwrite, QString *error) const
{
    auto fail = [&](const QString &text)
    {
        if (error != nullptr)
        {
            *error = text;
        }

        return false;
    };

    if (!loaded_)
    {
        return fail(QStringLiteral("Каталог игры не загружен."));
    }

    const QStringList problems = validateTrain(train);

    if (!problems.isEmpty())
    {
        return fail(problems.join(QLatin1Char('\n')));
    }

    const QString path = trainFilePath(train.name);

    if (QFileInfo::exists(path) && !overwrite)
    {
        return fail(QStringLiteral("Файл %1 уже существует.").arg(path));
    }

    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
    {
        return fail(QStringLiteral("Не удалось создать каталог %1.").arg(QFileInfo(path).absolutePath()));
    }

    QByteArray data;
    QXmlStreamWriter w(&data);
    w.setAutoFormatting(true);
    w.setAutoFormattingIndent(-1);   // табуляции, как в файлах игры
    w.writeStartDocument();
    w.writeStartElement(QStringLiteral("Config"));

    w.writeStartElement(QStringLiteral("Common"));
    w.writeComment(QStringLiteral(" Зарядное давление, МПа "));
    w.writeTextElement(QStringLiteral("ChargingPressure"), QString::number(train.chargingPressure, 'g', 6));
    w.writeComment(QStringLiteral(" Начальное давление в ГР, МПа "));
    w.writeTextElement(QStringLiteral("InitMainResPressure"), QString::number(train.initMainResPressure, 'g', 6));
    w.writeComment(QStringLiteral(" Поезд без воздуха (1 - без воздуха, 0 - полностью заряженный) "));
    w.writeTextElement(QStringLiteral("NoAir"), train.noAir ? QStringLiteral("1") : QStringLiteral("0"));

    for (const auto &extra : train.extraCommon)
    {
        w.writeTextElement(extra.first, extra.second);
    }

    w.writeTextElement(QStringLiteral("Title"), train.title.isEmpty() ? train.name : train.title);
    w.writeTextElement(QStringLiteral("Description"), train.description);
    w.writeEndElement();

    for (const ConsistGroup &g : train.groups)
    {
        if (g.count <= 0)
        {
            continue;
        }

        w.writeStartElement(QStringLiteral("Vehicle"));
        w.writeTextElement(QStringLiteral("ModuleDir"), g.module.moduleDir);
        w.writeTextElement(QStringLiteral("Module"), g.module.module);
        w.writeTextElement(QStringLiteral("ModuleConfigDir"), g.configDir);
        w.writeTextElement(QStringLiteral("ModuleConfig"), g.config);
        w.writeTextElement(QStringLiteral("IsOrientationForward"), g.forward ? QStringLiteral("1") : QStringLiteral("0"));
        w.writeTextElement(QStringLiteral("Count"), QString::number(g.count));
        w.writeTextElement(QStringLiteral("PayloadCoeff"), QString::number(g.payloadCoeff, 'g', 6));

        if (g.brakeShoes)
        {
            w.writeTextElement(QStringLiteral("IsBrakeShoes"), QStringLiteral("1"));
        }

        w.writeEndElement();
    }

    w.writeEndElement();
    w.writeEndDocument();

    QFile tmp(path + QStringLiteral(".tmp"));

    if (!tmp.open(QIODevice::WriteOnly | QIODevice::Truncate) || tmp.write(data) != data.size())
    {
        return fail(QStringLiteral("Не удалось записать %1: %2").arg(tmp.fileName(), tmp.errorString()));
    }

    tmp.close();

    if (QFileInfo::exists(path) && !QFile::remove(path))
    {
        QFile::remove(tmp.fileName());
        return fail(QStringLiteral("Не удалось заменить существующий файл %1.").arg(path));
    }

    if (!tmp.rename(path))
    {
        return fail(QStringLiteral("Не удалось сохранить %1: %2").arg(path, tmp.errorString()));
    }

    return true;
}

} // namespace rse
