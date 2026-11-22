#include    <QtTest>

#include    <QDir>
#include    <QFile>
#include    <QTemporaryDir>

#include    "catalog/vehicle-catalog.h"

using namespace rse;

namespace
{

//------------------------------------------------------------------------------
// Записать текстовый файл (UTF-8), создав каталоги
//------------------------------------------------------------------------------
void writeFile(const QString &path, const QString &content)
{
    QDir().mkpath(QFileInfo(path).absolutePath());

    QFile f(path);
    QVERIFY2(f.open(QIODevice::WriteOnly), qPrintable(path));
    f.write(content.toUtf8());
}

//------------------------------------------------------------------------------
// Записать файл как есть (байты, без перекодирования)
//------------------------------------------------------------------------------
void writeRaw(const QString &path, const QByteArray &content)
{
    QDir().mkpath(QFileInfo(path).absolutePath());

    QFile f(path);
    QVERIFY2(f.open(QIODevice::WriteOnly), qPrintable(path));
    f.write(content);
}

//------------------------------------------------------------------------------
// Конфиг единицы: главный конфиг с секцией Vehicle
//------------------------------------------------------------------------------
QString vehicleXml(double length, double empty, double payload,
                   bool crane, bool autopilot)
{
    QString s = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<Config>\n<Vehicle>\n";

    s += QString("<EmptyMass>%1</EmptyMass>\n<PayloadMass>%2</PayloadMass>\n"
                 "<Length>%3</Length>\n").arg(empty).arg(payload).arg(length);

    // Закомментированный тег не должен учитываться
    s += "<!-- <LocoCraneModule>ignored</LocoCraneModule> -->\n";

    if (crane)
    {
        s += "<BrakeCraneModule>krm394</BrakeCraneModule>\n";
    }

    if (autopilot)
    {
        s += "<AutopilotModule>some-autopilot</AutopilotModule>\n";
    }
    else
    {
        s += "<AutopilotModule></AutopilotModule>\n";
    }

    s += "</Vehicle>\n<Cabine><DriverPos>1 2 3</DriverPos></Cabine>\n</Config>\n";

    return s;
}

//------------------------------------------------------------------------------
// Секция Vehicle в конфиге состава
//------------------------------------------------------------------------------
QString groupXml(const QString &module_dir, const QString &module,
                 const QString &cfg_dir, const QString &cfg,
                 int count, double payload_coeff = 0.0)
{
    return QString("<Vehicle>\n<ModuleDir>%1</ModuleDir>\n<Module>%2</Module>\n"
                   "<ModuleConfigDir>%3</ModuleConfigDir>\n<ModuleConfig>%4</ModuleConfig>\n"
                   "<IsOrientationForward>1</IsOrientationForward>\n<Count>%5</Count>\n"
                   "<PayloadCoeff>%6</PayloadCoeff>\n</Vehicle>\n")
        .arg(module_dir, module, cfg_dir, cfg).arg(count).arg(payload_coeff);
}

QString trainXml(const QString &title, const QString &groups)
{
    return QString("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<Config>\n<Common>\n"
                   "<ChargingPressure>0.52</ChargingPressure>\n<NoAir>1</NoAir>\n"
                   "<Title>%1</Title>\n<Description>Описание</Description>\n</Common>\n%2</Config>\n")
        .arg(title, groups);
}

//------------------------------------------------------------------------------
// Игра из одной единицы cars/c1 и составов, каждый из которых называет для неё
// свой модуль (модуль "каталог/имя" задаётся парой ModuleDir и Module)
//------------------------------------------------------------------------------
void buildTinyGame(const QString &root, const QStringList &train_modules)
{
    writeFile(root + "/cfg/vehicles/cars/c1.xml", vehicleXml(10.0, 1000.0, 2000.0, false, false));

    int n = 0;

    for (const QString &id : train_modules)
    {
        const QString dir = id.section('/', 0, 0);
        const QString mod = id.section('/', 1, 1);

        writeFile(root + QString("/cfg/trains/t%1.xml").arg(++n),
                  trainXml("t", groupXml(dir, mod, "cars", "c1", 1)));
    }
}

//------------------------------------------------------------------------------
// Небольшая игра с известными ответами
//------------------------------------------------------------------------------
void buildGame(const QString &root)
{
    // Локомотив с автопилотом
    writeFile(root + "/cfg/vehicles/locoD/locoD-1.xml",
              vehicleXml(21.0, 100000.0, 1000.0, true, true));

    // Вагоны: carA указан в составе, carB - нет (привязка по каталогу)
    writeFile(root + "/cfg/vehicles/cars/carA.xml", vehicleXml(20.0, 20000.0, 60000.0, false, false));
    writeFile(root + "/cfg/vehicles/cars/carB.xml", vehicleXml(10.0, 10000.0, 30000.0, false, false));

    // Вспомогательные файлы единицами не считаются
    writeFile(root + "/cfg/vehicles/cars/pneumo-hose.xml",
              "<?xml version=\"1.0\"?><Config><Device><Volume>1</Volume></Device></Config>");
    writeFile(root + "/cfg/vehicles/cars/iocontroller.xml",
              "<?xml version=\"1.0\"?><Config><Common><A>1</A></Common></Config>");

    // Каталог, которого нет ни в одном составе
    writeFile(root + "/cfg/vehicles/lonely/x.xml", vehicleXml(15.0, 1.0, 1.0, false, false));

    // Каталог, который в составах привязан к разным модулям
    writeFile(root + "/cfg/vehicles/mixed/y.xml", vehicleXml(12.0, 1.0, 1.0, false, false));

    writeFile(root + "/cfg/trains/t1.xml",
              trainXml("Грузовой",
                       groupXml("locoD", "locoDmod", "locoD", "locoD-1", 1, 1.0)
                       + groupXml("carsmod", "carsmod", "cars", "carA", 3, 0.5)));

    writeFile(root + "/cfg/trains/t2.xml",
              trainXml("Смешанный",
                       groupXml("m1", "m1", "mixed", "other1", 1)
                       + groupXml("m2", "m2", "mixed", "other2", 1)));

    // Битый XML не должен ронять загрузку
    writeFile(root + "/cfg/trains/broken.xml", "<Config><Common><Title>oops</Common></Config>");

    // Вспомогательный файл с битым XML единицей не является и шума не даёт
    writeFile(root + "/cfg/vehicles/cars/phase-splitter.xml",
              "<?xml version=\"1.0\"?>\n<Config>\n<Device>\n<Order>1</Order>\n</Config>\n");

    // Установленные модули: одна библиотека с префиксом lib, другая без
    writeFile(root + "/modules/locoD/liblocoDmod.so", "x");
    writeFile(root + "/modules/carsmod/carsmod.dll", "x");
}

} // namespace

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
class TestCatalog : public QObject
{
    Q_OBJECT

private slots:

    void gameRootFromRoute();
    void rejectsNonGameDirectory();
    void findsOnlyMainVehicleConfigs();
    void readsVehicleFields();
    void detectsLocomotiveAndAutopilot();
    void readsTrains();
    void resolvesModulesByPriority();
    void ambiguousDirectoryIsNotGuessed();
    void manualModuleOverrides();
    void manualModulesSurviveReload();
    void manualModulesJsonRoundTrip();
    void findsInstalledModules();
    void summarizesTrain();
    void summaryReportsMissingVehicles();
    void warnsAboutDanglingReferences();
    void auxiliaryBrokenXmlIsQuiet();
    void toleratesNonUtf8Comment();
    void majorityModuleWins();
    void installedModuleBreaksTie();
    void tieWithoutModulesIsUnresolved();
    void realGame();
    void realGameWithInstalledModules();
};

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void TestCatalog::gameRootFromRoute()
{
    // Пути могут не существовать: определение корня работает со строками
    QCOMPARE(VehicleCatalog::gameRootForRoute("/x/game/routes/r1"), QString("/x/game"));
    QCOMPARE(VehicleCatalog::gameRootForRoute("/x/game/routes/r1/"), QString("/x/game"));
    QCOMPARE(VehicleCatalog::gameRootForRoute("/x/game/routes/../routes/r1"), QString("/x/game"));

    // Слишком короткий путь корня игры не даёт
    QVERIFY(VehicleCatalog::gameRootForRoute("/").isEmpty());
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void TestCatalog::rejectsNonGameDirectory()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    VehicleCatalog c;

    QVERIFY(!c.load(tmp.path()));
    QVERIFY(!c.errorString().isEmpty());
    QVERIFY(!c.isLoaded());

    // Неудачная загрузка не стирает ранее загруженный каталог
    QTemporaryDir game;
    buildGame(game.path());

    QVERIFY(c.load(game.path()));
    const int vehicles = c.vehicles().size();

    QVERIFY(!c.load(tmp.path()));
    QVERIFY(c.isLoaded());
    QCOMPARE(c.vehicles().size(), vehicles);
    QVERIFY(!c.errorString().isEmpty());
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void TestCatalog::findsOnlyMainVehicleConfigs()
{
    QTemporaryDir tmp;
    buildGame(tmp.path());

    VehicleCatalog c;
    QVERIFY2(c.load(tmp.path()), qPrintable(c.errorString()));

    // locoD-1, carA, carB, x, y; файлы pneumo-hose и iocontroller пропущены
    QCOMPARE(c.vehicles().size(), 5);
    QVERIFY(c.findVehicle("cars", "pneumo-hose") == nullptr);
    QVERIFY(c.findVehicle("cars", "iocontroller") == nullptr);
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void TestCatalog::readsVehicleFields()
{
    QTemporaryDir tmp;
    buildGame(tmp.path());

    VehicleCatalog c;
    QVERIFY(c.load(tmp.path()));

    const VehicleConfigData *a = c.findVehicle("cars", "carA");
    QVERIFY(a != nullptr);
    QCOMPARE(a->length, 20.0);
    QCOMPARE(a->emptyMass, 20000.0);
    QCOMPARE(a->payloadMass, 60000.0);
    QCOMPARE(a->id(), QString("cars/carA"));
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void TestCatalog::detectsLocomotiveAndAutopilot()
{
    QTemporaryDir tmp;
    buildGame(tmp.path());

    VehicleCatalog c;
    QVERIFY(c.load(tmp.path()));

    const VehicleConfigData *loco = c.findVehicle("locoD", "locoD-1");
    QVERIFY(loco != nullptr);
    QVERIFY(loco->isLocomotive);
    QVERIFY(loco->hasAutopilot);

    // Закомментированный LocoCraneModule и пустой AutopilotModule не считаются
    const VehicleConfigData *car = c.findVehicle("cars", "carA");
    QVERIFY(car != nullptr);
    QVERIFY(!car->isLocomotive);
    QVERIFY(!car->hasAutopilot);
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void TestCatalog::readsTrains()
{
    QTemporaryDir tmp;
    buildGame(tmp.path());

    VehicleCatalog c;
    QVERIFY(c.load(tmp.path()));

    // t1 и t2; broken.xml пропущен с замечанием
    QCOMPARE(c.trains().size(), 2);

    const TrainConfigData *t1 = c.findTrain("t1");
    QVERIFY(t1 != nullptr);
    QCOMPARE(t1->title, QString("Грузовой"));
    QCOMPARE(t1->description, QString("Описание"));
    QVERIFY(t1->noAir);
    QCOMPARE(t1->chargingPressure, 0.52);
    QCOMPARE(t1->initMainResPressure, 0.9);
    QCOMPARE(t1->groups.size(), 2);
    QCOMPARE(t1->groups.at(0).module.id(), QString("locoD/locoDmod"));
    QCOMPARE(t1->groups.at(1).count, 3);
    QCOMPARE(t1->groups.at(1).payloadCoeff, 0.5);

    bool broken_reported = false;

    for (const QString &w : c.warnings())
    {
        if (w.contains("broken.xml"))
        {
            broken_reported = true;
        }
    }

    QVERIFY2(broken_reported, "ожидалось замечание о битом broken.xml");
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void TestCatalog::resolvesModulesByPriority()
{
    QTemporaryDir tmp;
    buildGame(tmp.path());

    VehicleCatalog c;
    QVERIFY(c.load(tmp.path()));

    // Единица прямо указана в составе
    const VehicleConfigData *a = c.findVehicle("cars", "carA");
    QCOMPARE(a->module.id(), QString("carsmod/carsmod"));
    QVERIFY(a->moduleSource == ModuleSource::TrainExact);

    // carB в составах нет, но весь каталог cars привязан к одному модулю
    const VehicleConfigData *b = c.findVehicle("cars", "carB");
    QCOMPARE(b->module.id(), QString("carsmod/carsmod"));
    QVERIFY(b->moduleSource == ModuleSource::TrainDir);

    // Каталог lonely не встречается ни в одном составе
    const VehicleConfigData *x = c.findVehicle("lonely", "x");
    QVERIFY(!x->module.isValid());
    QVERIFY(x->moduleSource == ModuleSource::Unknown);
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void TestCatalog::ambiguousDirectoryIsNotGuessed()
{
    QTemporaryDir tmp;
    buildGame(tmp.path());

    VehicleCatalog c;
    QVERIFY(c.load(tmp.path()));

    // В t2 каталог mixed привязан и к m1, и к m2: угадывать нельзя
    const VehicleConfigData *y = c.findVehicle("mixed", "y");
    QVERIFY(y != nullptr);
    QVERIFY(!y->module.isValid());

    bool reported = false;

    for (const QString &w : c.warnings())
    {
        if (w.contains("mixed") && w.contains("разными"))
        {
            reported = true;
        }
    }

    QVERIFY2(reported, "ожидалось замечание о неоднозначном каталоге mixed");
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void TestCatalog::manualModuleOverrides()
{
    QTemporaryDir tmp;
    buildGame(tmp.path());

    VehicleCatalog c;
    QVERIFY(c.load(tmp.path()));

    const int before = c.unresolvedCount();
    QVERIFY(before >= 2);

    // Ручная привязка применяется сразу
    ModuleRef m;
    m.moduleDir = "custom";
    m.module = "custommod";
    c.setManualModule("lonely", m);

    const VehicleConfigData *x = c.findVehicle("lonely", "x");
    QCOMPARE(x->module.id(), QString("custom/custommod"));
    QVERIFY(x->moduleSource == ModuleSource::Manual);
    QCOMPARE(c.unresolvedCount(), before - 1);

    // Ручная привязка сильнее привязки из составов
    ModuleRef other;
    other.moduleDir = "other";
    other.module = "othermod";
    c.setManualModule("cars", other);
    QCOMPARE(c.findVehicle("cars", "carA")->module.id(), QString("other/othermod"));

    // Пустая ссылка снимает привязку, возвращая привязку из составов
    c.setManualModule("cars", ModuleRef());
    QCOMPARE(c.findVehicle("cars", "carA")->module.id(), QString("carsmod/carsmod"));

    // Только имя модуля: каталог берётся одноимённый, как в симуляторе
    ModuleRef short_ref;
    short_ref.module = "solo";
    c.setManualModule("lonely", short_ref);
    QCOMPARE(c.findVehicle("lonely", "x")->module.id(), QString("solo/solo"));
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void TestCatalog::manualModulesSurviveReload()
{
    QTemporaryDir tmp;
    buildGame(tmp.path());

    VehicleCatalog c;

    // Привязка задана до загрузки каталога
    ModuleRef m;
    m.moduleDir = "custom";
    m.module = "custommod";
    c.setManualModule("lonely", m);

    QVERIFY(c.load(tmp.path()));
    QCOMPARE(c.findVehicle("lonely", "x")->module.id(), QString("custom/custommod"));

    // И после повторной загрузки
    QVERIFY(c.load(tmp.path()));
    QCOMPARE(c.findVehicle("lonely", "x")->module.id(), QString("custom/custommod"));
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void TestCatalog::manualModulesJsonRoundTrip()
{
    QTemporaryDir tmp;
    const QString path = tmp.path() + "/sub/dir/vehicle-modules.json";

    VehicleCatalog a;

    ModuleRef m;
    m.moduleDir = "custom";
    m.module = "custommod";
    a.setManualModule("lonely", m);

    QVERIFY(a.saveManualModules(path));
    QVERIFY(QFile::exists(path));

    VehicleCatalog b;
    QVERIFY(b.loadManualModules(path));
    QCOMPARE(b.manualModules().size(), 1);
    QCOMPARE(b.manualModules().value("lonely").id(), QString("custom/custommod"));

    // Отсутствующий файл - не ошибка
    VehicleCatalog d;
    QVERIFY(d.loadManualModules(tmp.path() + "/nothing.json"));
    QVERIFY(d.manualModules().isEmpty());

    // Испорченный файл - ошибка
    writeFile(tmp.path() + "/bad.json", "{ not json");
    VehicleCatalog e;
    QVERIFY(!e.loadManualModules(tmp.path() + "/bad.json"));
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void TestCatalog::findsInstalledModules()
{
    QTemporaryDir tmp;
    buildGame(tmp.path());

    VehicleCatalog c;
    QVERIFY(c.load(tmp.path()));

    QVERIFY(c.hasModulesDir());

    // Префикс lib отбрасывается
    QVERIFY(c.installedModules().contains("locoD/locoDmod"));
    QVERIFY(c.installedModules().contains("carsmod/carsmod"));

    // Модули m1/m2 из состава t2 не установлены, но единиц mixed/y с этой привязкой нет,
    // поэтому замечания о них нет. Проверим на модуле, который реально назначен
    ModuleRef m;
    m.moduleDir = "ghost";
    m.module = "ghostmod";
    c.setManualModule("lonely", m);

    bool reported = false;

    for (const QString &w : c.warnings())
    {
        if (w.contains("ghost/ghostmod") && w.contains("не найден"))
        {
            reported = true;
        }
    }

    QVERIFY2(reported, "ожидалось замечание о неустановленном модуле");
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void TestCatalog::summarizesTrain()
{
    QTemporaryDir tmp;
    buildGame(tmp.path());

    VehicleCatalog c;
    QVERIFY(c.load(tmp.path()));

    const TrainConfigData *t1 = c.findTrain("t1");
    QVERIFY(t1 != nullptr);

    const TrainSummary s = c.summarize(*t1);

    QCOMPARE(s.vehicles, 4);
    QCOMPARE(s.locomotives, 1);
    QCOMPARE(s.length, 21.0 + 3 * 20.0);

    // Локомотив 100000 + 1000 * 1.0; три вагона по 20000 + 60000 * 0.5
    QCOMPARE(s.mass, 101000.0 + 3 * 50000.0);

    QVERIFY(s.complete);
    QVERIFY(s.autopilotCapable);
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void TestCatalog::summaryReportsMissingVehicles()
{
    QTemporaryDir tmp;
    buildGame(tmp.path());

    VehicleCatalog c;
    QVERIFY(c.load(tmp.path()));

    QVector<ConsistGroup> groups;

    ConsistGroup car;
    car.configDir = "cars";
    car.config = "carA";
    car.count = 2;
    groups.push_back(car);

    ConsistGroup ghost;
    ghost.configDir = "cars";
    ghost.config = "no-such-car";
    ghost.count = 5;
    groups.push_back(ghost);

    // Нулевая и отрицательная группы игнорируются
    ConsistGroup zero;
    zero.configDir = "cars";
    zero.config = "carB";
    zero.count = 0;
    groups.push_back(zero);

    const TrainSummary s = c.summarize(groups);

    QCOMPARE(s.vehicles, 7);
    QCOMPARE(s.length, 40.0);
    QVERIFY(!s.complete);

    // Автопилота нет: крайние единицы - вагон и неизвестная единица
    QVERIFY(!s.autopilotCapable);

    // Пустой состав
    const TrainSummary empty = c.summarize(QVector<ConsistGroup>());
    QCOMPARE(empty.vehicles, 0);
    QVERIFY(empty.complete);
    QVERIFY(!empty.autopilotCapable);

    // Локомотив в середине состава автоведения не даёт
    QVector<ConsistGroup> mid;

    ConsistGroup loco;
    loco.configDir = "locoD";
    loco.config = "locoD-1";
    loco.count = 1;

    mid.push_back(car);
    mid.push_back(loco);
    mid.push_back(car);

    QVERIFY(!c.summarize(mid).autopilotCapable);

    // Локомотив в хвосте - даёт
    QVector<ConsistGroup> tail;
    tail.push_back(car);
    tail.push_back(loco);

    QVERIFY(c.summarize(tail).autopilotCapable);
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void TestCatalog::warnsAboutDanglingReferences()
{
    QTemporaryDir tmp;
    buildGame(tmp.path());

    VehicleCatalog c;
    QVERIFY(c.load(tmp.path()));

    // В t2 указаны единицы mixed/other1 и mixed/other2, которых нет в cfg/vehicles
    bool reported = false;

    for (const QString &w : c.warnings())
    {
        if (w.contains("t2") && w.contains("mixed/other1"))
        {
            reported = true;
        }
    }

    QVERIFY2(reported, "ожидалось замечание о единице mixed/other1");
}

//------------------------------------------------------------------------------
// Проверка на реальных данных игры (корень игры выводится из RRS_ROUTE_DIR)
//------------------------------------------------------------------------------
void TestCatalog::realGame()
{
    const QString route = qEnvironmentVariable("RRS_ROUTE_DIR");

    if (route.isEmpty())
    {
        QSKIP("RRS_ROUTE_DIR не задан: проверка на реальных данных пропущена");
    }

    const QString root = VehicleCatalog::gameRootForRoute(route);
    QVERIFY2(VehicleCatalog::isGameRoot(root), qPrintable(root));

    VehicleCatalog c;
    QVERIFY2(c.load(root), qPrintable(c.errorString()));

    qInfo() << "Единиц:" << c.vehicles().size() << "Составов:" << c.trains().size()
            << "Без модуля:" << c.unresolvedCount();

    for (const QString &w : c.warnings())
    {
        qInfo().noquote() << "  " << w;
    }

    // Числа получены независимым подсчётом по файлам cfg. Файл
    // vl60pk-1543-poezd2.xml содержит комментарий в cp1251, но читается
    QCOMPARE(c.vehicles().size(), 10);
    QCOMPARE(c.trains().size(), 10);

    // Каталога modules в исходниках игры нет, поэтому выбирать между вариантами
    // приходится по составам. vl60k-1737: в трёх составах vl60/vl60k и в одном
    // vl60k/vl60k - побеждает большинство
    const VehicleConfigData *k = c.findVehicle("vl60k", "vl60k-1737");
    QVERIFY(k != nullptr);
    QCOMPARE(k->module.id(), QString("vl60/vl60k"));
    QVERIFY(k->moduleSource == ModuleSource::TrainMajority);

    // vl60pk-1543: по два состава на vl60/vl60pk и vl60pk/vl60pk - ничья,
    // угадывать нельзя
    const VehicleConfigData *pk = c.findVehicle("vl60pk", "vl60pk-1543");
    QVERIFY(pk != nullptr);
    QCOMPARE(pk->length, 21.0);
    QVERIFY(pk->isLocomotive);
    QVERIFY(pk->hasAutopilot);
    QVERIFY(!pk->module.isValid());
    QCOMPARE(c.unresolvedCount(), 1);

    bool tie_reported = false;
    bool utf8_reported = false;

    for (const QString &w : c.warnings())
    {
        if (w.contains("vl60pk-1543") && w.contains("выбрать не удалось"))
        {
            tie_reported = true;
        }

        if (w.contains("poezd2") && w.contains("UTF-8"))
        {
            utf8_reported = true;
        }

        // Вспомогательные файлы приборов единицами не являются
        QVERIFY2(!w.contains("phase-splitter"), qPrintable(w));
    }

    QVERIFY2(tie_reported, "ожидалось замечание о ничьей для vl60pk-1543");
    QVERIFY2(utf8_reported, "ожидалось замечание о кодировке poezd2.xml");

    const VehicleConfigData *car = c.findVehicle("IMR_pass_rzd", "IMR_pass_rzd-13819");
    QVERIFY(car != nullptr);
    QCOMPARE(car->length, 25.0);
    QVERIFY(!car->isLocomotive);
    QCOMPARE(car->module.id(), QString("passcar/passcar"));

    const VehicleConfigData *hopper = c.findVehicle("Fr_hopper_RZD", "Fr_hopper_RZD-2851");
    QVERIFY(hopper != nullptr);
    QCOMPARE(hopper->length, 14.5);
    QCOMPARE(hopper->module.id(), QString("freightcar/freightcar"));

    // Грузовой состав без локомотива: 1 + 48 + 1 вагон по 14,5 м
    const TrainConfigData *cargo = c.findTrain("cargo-train");
    QVERIFY(cargo != nullptr);

    const TrainSummary cs = c.summarize(*cargo);
    QCOMPARE(cs.vehicles, 50);
    QCOMPARE(cs.locomotives, 0);
    QCOMPARE(cs.length, 50 * 14.5);
    QVERIFY(cs.complete);
    QVERIFY(!cs.autopilotCapable);

    // ВЛ60пк с пассажирским составом: локомотив впереди, 17 вагонов
    const TrainConfigData *p65 = c.findTrain("vl60pk-1543-T65_17");
    QVERIFY(p65 != nullptr);

    const TrainSummary ps = c.summarize(*p65);
    QCOMPARE(ps.vehicles, 18);
    QCOMPARE(ps.locomotives, 1);
    QVERIFY(ps.complete);
    QVERIFY(ps.autopilotCapable);

    // Состав из одного локомотива
    const TrainConfigData *solo = c.findTrain("vl60pk-1543");
    QVERIFY(solo != nullptr);
    QCOMPARE(c.summarize(*solo).length, 21.0);

    // Все составы игры должны считаться полностью
    for (const TrainConfigData &t : c.trains())
    {
        QVERIFY2(c.summarize(t).complete, qPrintable(t.name));
        QVERIFY2(!t.title.isEmpty(), qPrintable(t.name));
    }
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void TestCatalog::auxiliaryBrokenXmlIsQuiet()
{
    QTemporaryDir tmp;
    buildGame(tmp.path());

    VehicleCatalog c;
    QVERIFY(c.load(tmp.path()));

    // Файл phase-splitter.xml в каталоге единиц разобрать нельзя, но в нём нет Length
    for (const QString &w : c.warnings())
    {
        QVERIFY2(!w.contains("phase-splitter"), qPrintable(w));
    }

    // А битый файл, похожий на единицу (есть Length), пропускать молча нельзя
    writeFile(tmp.path() + "/cfg/vehicles/cars/carC.xml",
              "<Config><Vehicle><Length>5</Length></Config>");

    QVERIFY(c.load(tmp.path()));

    bool reported = false;

    for (const QString &w : c.warnings())
    {
        if (w.contains("carC.xml"))
        {
            reported = true;
        }
    }

    QVERIFY2(reported, "ожидалось замечание о битом carC.xml");
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void TestCatalog::toleratesNonUtf8Comment()
{
    QTemporaryDir tmp;
    buildTinyGame(tmp.path(), { "m/m" });

    // Объявление UTF-8, но комментарий в cp1251: "Купейные (К)"
    QByteArray xml = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<Config>\n"
                     "\t<Common><Title>T</Title></Common>\n"
                     "\t<!-- \xca\xf3\xef\xe5\xe9\xed\xfb\xe5 (\xca) -->\n"
                     "\t<Vehicle><ModuleDir>m</ModuleDir><Module>m</Module>"
                     "<ModuleConfigDir>cars</ModuleConfigDir><ModuleConfig>c1</ModuleConfig>"
                     "<Count>2</Count></Vehicle>\n</Config>\n";

    writeRaw(tmp.path() + "/cfg/trains/cp1251.xml", xml);

    VehicleCatalog c;
    QVERIFY(c.load(tmp.path()));

    // Состав прочитан несмотря на испорченный комментарий
    const TrainConfigData *t = c.findTrain("cp1251");
    QVERIFY(t != nullptr);
    QCOMPARE(t->title, QString("T"));
    QCOMPARE(t->groups.size(), 1);
    QCOMPARE(t->groups.first().count, 2);

    bool reported = false;

    for (const QString &w : c.warnings())
    {
        if (w.contains("cp1251.xml") && w.contains("UTF-8"))
        {
            reported = true;
        }
    }

    QVERIFY2(reported, "ожидалось замечание о кодировке");
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void TestCatalog::majorityModuleWins()
{
    QTemporaryDir tmp;

    // Три состава называют модуль X/X, один - Y/Y
    buildTinyGame(tmp.path(), { "X/X", "X/X", "Y/Y", "X/X" });

    VehicleCatalog c;
    QVERIFY(c.load(tmp.path()));

    const VehicleConfigData *v = c.findVehicle("cars", "c1");
    QVERIFY(v != nullptr);
    QCOMPARE(v->module.id(), QString("X/X"));
    QVERIFY(v->moduleSource == ModuleSource::TrainMajority);

    bool reported = false;

    for (const QString &w : c.warnings())
    {
        if (w.contains("cars/c1") && w.contains("разными") && w.contains("выбран X/X"))
        {
            reported = true;
        }
    }

    QVERIFY2(reported, "ожидалось замечание о расхождении и выбранном модуле");
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void TestCatalog::installedModuleBreaksTie()
{
    QTemporaryDir tmp;

    // Ничья 1:1, но установлен только Y/Y: симулятор загрузит именно его
    buildTinyGame(tmp.path(), { "X/X", "Y/Y" });
    writeFile(tmp.path() + "/modules/Y/libY.so", "x");

    VehicleCatalog c;
    QVERIFY(c.load(tmp.path()));

    const VehicleConfigData *v = c.findVehicle("cars", "c1");
    QVERIFY(v != nullptr);
    QCOMPARE(v->module.id(), QString("Y/Y"));
    QVERIFY(v->moduleSource == ModuleSource::TrainExact);
    QCOMPARE(c.unresolvedCount(), 0);

    // Состав, который называет неустановленный модуль X/X, не загрузится
    bool reported = false;

    for (const QString &w : c.warnings())
    {
        if (w.contains("Состав t1") && w.contains("X/X") && w.contains("не загрузит"))
        {
            reported = true;
        }
    }

    QVERIFY2(reported, "ожидалось замечание о составе t1 с неустановленным модулем");
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void TestCatalog::tieWithoutModulesIsUnresolved()
{
    QTemporaryDir tmp;

    // Ничья 1:1 и каталога modules нет: выбрать нельзя
    buildTinyGame(tmp.path(), { "X/X", "Y/Y" });

    VehicleCatalog c;
    QVERIFY(c.load(tmp.path()));

    const VehicleConfigData *v = c.findVehicle("cars", "c1");
    QVERIFY(v != nullptr);
    QVERIFY(!v->module.isValid());
    QCOMPARE(c.unresolvedCount(), 1);

    bool reported = false;

    for (const QString &w : c.warnings())
    {
        if (w.contains("cars/c1") && w.contains("выбрать не удалось"))
        {
            reported = true;
        }
    }

    QVERIFY2(reported, "ожидалось замечание о ничьей");

    // Ручное назначение решает вопрос
    ModuleRef m;
    m.moduleDir = "X";
    m.module = "X";
    c.setManualModule("cars", m);

    QCOMPARE(c.findVehicle("cars", "c1")->module.id(), QString("X/X"));
    QCOMPARE(c.unresolvedCount(), 0);
}

//------------------------------------------------------------------------------
// Реальные cfg игры, но с каталогом modules, как у пользователя с установленной
// игрой: имитируем его пустыми файлами библиотек
//------------------------------------------------------------------------------
void TestCatalog::realGameWithInstalledModules()
{
    const QString route = qEnvironmentVariable("RRS_ROUTE_DIR");

    if (route.isEmpty())
    {
        QSKIP("RRS_ROUTE_DIR не задан: проверка на реальных данных пропущена");
    }

    const QString real_root = VehicleCatalog::gameRootForRoute(route);

    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    if (!QFile::link(real_root + "/cfg", tmp.path() + "/cfg"))
    {
        QSKIP("Не удалось создать символическую ссылку на cfg");
    }

    // Библиотеки лежат в modules/vl60 (так их собирает CMake игры), а не в modules/vl60pk
    for (const char *lib : { "vl60/libvl60pk.so", "vl60/libvl60k.so",
                             "passcar/libpasscar.so", "freightcar/libfreightcar.so" })
    {
        writeFile(tmp.path() + "/modules/" + lib, "x");
    }

    VehicleCatalog c;
    QVERIFY2(c.load(tmp.path()), qPrintable(c.errorString()));

    // Теперь неоднозначности снимает установленный набор модулей
    QCOMPARE(c.unresolvedCount(), 0);

    const VehicleConfigData *pk = c.findVehicle("vl60pk", "vl60pk-1543");
    QVERIFY(pk != nullptr);
    QCOMPARE(pk->module.id(), QString("vl60/vl60pk"));
    QVERIFY(pk->moduleSource == ModuleSource::TrainExact);

    const VehicleConfigData *k = c.findVehicle("vl60k", "vl60k-1737");
    QVERIFY(k != nullptr);
    QCOMPARE(k->module.id(), QString("vl60/vl60k"));
    QVERIFY(k->moduleSource == ModuleSource::TrainExact);

    // Готовые составы со старыми ModuleDir (vl60k, vl60pk) указывают на каталоги,
    // которых в modules нет: такие составы игра не загрузит
    QStringList stale;

    for (const QString &w : c.warnings())
    {
        if (w.startsWith("Состав ") && w.contains("не загрузит"))
        {
            stale << w;
        }
    }

    qInfo() << "Составы с неустановленными модулями:" << stale.size();

    for (const QString &w : std::as_const(stale))
    {
        qInfo().noquote() << "  " << w;
    }

    QCOMPARE(stale.size(), 3);

    auto mentions = [&](const QString &train)
    {
        for (const QString &w : stale)
        {
            if (w.startsWith("Состав " + train + ":"))
            {
                return true;
            }
        }

        return false;
    };

    QVERIFY(mentions("VL60k-poezd2342"));
    QVERIFY(mentions("vl60pk-1543-T33_15"));
    QVERIFY(mentions("vl60pk-1543-poezd2"));
}

QTEST_APPLESS_MAIN(TestCatalog)

#include "test-catalog.moc"
