#include    <QtTest>

#include    <QDir>
#include    <QFile>
#include    <QFileInfo>
#include    <QImage>
#include    <QTemporaryDir>

#include    "catalog/vehicle-catalog.h"
#include    "ui/rolling-stock-panel.h"

using namespace rse;

namespace
{

//------------------------------------------------------------------------------
// Сохранить картинку, если задан каталог RSE_SHOT_DIR (для просмотра глазами)
//------------------------------------------------------------------------------
void saveShot(const QImage &img, const QString &name)
{
    const QString dir = qEnvironmentVariable("RSE_SHOT_DIR");

    if (!dir.isEmpty())
    {
        img.save(dir + "/" + name);
    }
}

} // namespace

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
class TestPanel : public QObject
{
    Q_OBJECT

private slots:

    void showsHintWithoutCatalog();
    void showsRealCatalog();
    void filterNarrowsRows();
    void tabsSwitchRows();
};

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void TestPanel::showsHintWithoutCatalog()
{
    RollingStockPanel panel;
    panel.resize(520, 500);

    QCOMPARE(panel.visibleVehicleCount(), 0);
    QCOMPARE(panel.visibleTrainCount(), 0);

    // Каталог есть, но не загружен
    VehicleCatalog c;
    panel.setCatalog(&c);

    QCOMPARE(panel.visibleVehicleCount(), 0);
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void TestPanel::showsRealCatalog()
{
    const QString route = qEnvironmentVariable("RRS_ROUTE_DIR");

    if (route.isEmpty())
    {
        QSKIP("RRS_ROUTE_DIR не задан");
    }

    VehicleCatalog c;
    QVERIFY(c.load(VehicleCatalog::gameRootForRoute(route)));

    RollingStockPanel panel;
    panel.resize(620, 560);
    panel.setCatalog(&c);

    QCOMPARE(panel.visibleVehicleCount(), 10);
    QCOMPARE(panel.visibleTrainCount(), 10);

    saveShot(panel.grab().toImage(), "panel.png");
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void TestPanel::filterNarrowsRows()
{
    const QString route = qEnvironmentVariable("RRS_ROUTE_DIR");

    if (route.isEmpty())
    {
        QSKIP("RRS_ROUTE_DIR не задан");
    }

    VehicleCatalog c;
    QVERIFY(c.load(VehicleCatalog::gameRootForRoute(route)));

    RollingStockPanel panel;
    panel.resize(620, 560);
    panel.setCatalog(&c);

    // Совпал каталог конфигов: видны все его единицы (7 пассажирских вагонов),
    // составы - только те, где такие вагоны есть
    panel.setFilter("imr_pass");
    QCOMPARE(panel.visibleVehicleCount(), 7);
    QCOMPARE(panel.visibleTrainCount(), 4);

    // Регистр не важен
    panel.setFilter("IMR_PASS");
    QCOMPARE(panel.visibleVehicleCount(), 7);

    // Совпала одна единица
    panel.setFilter("1543");
    QCOMPARE(panel.visibleVehicleCount(), 1);

    // Совпал состав целиком (по названию): единицы не показываются
    panel.setFilter("cargo-train");
    QCOMPARE(panel.visibleTrainCount(), 1);
    QCOMPARE(panel.visibleVehicleCount(), 0);

    // Ничего не найдено
    panel.setFilter("нет-такого");
    QCOMPARE(panel.visibleVehicleCount(), 0);
    QCOMPARE(panel.visibleTrainCount(), 0);

    saveShot(panel.grab().toImage(), "panel-empty-filter.png");

    // Сброс фильтра возвращает всё
    panel.setFilter("");
    QCOMPARE(panel.visibleVehicleCount(), 10);
    QCOMPARE(panel.visibleTrainCount(), 10);
}

//------------------------------------------------------------------------------
// Вкладки «Единицы» / «Готовые составы» (аудит 2.7): переключаются кнопками,
// selectTrain сам переходит на вкладку составов
//------------------------------------------------------------------------------
void TestPanel::tabsSwitchRows()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString root = dir.path();

    // Минимальный каталог: две единицы и один готовый состав (как в test-export)
    const auto write = [root](const QString &rel, const QString &body)
    {
        QFile f(QDir(root).filePath(rel));
        QDir().mkpath(QFileInfo(f).absolutePath());
        QVERIFY2(f.open(QIODevice::WriteOnly), qPrintable(rel));
        f.write(body.toUtf8());
    };

    write(QStringLiteral("cfg/vehicles/locos/L.xml"),
          QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<Config>\n<Vehicle>\n"
                         "<EmptyMass>100000</EmptyMass>\n<PayloadMass>0</PayloadMass>\n"
                         "<Length>20</Length>\n<AutopilotModule></AutopilotModule>\n"
                         "</Vehicle>\n</Config>\n"));
    write(QStringLiteral("cfg/vehicles/cars/W.xml"),
          QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<Config>\n<Vehicle>\n"
                         "<EmptyMass>10000</EmptyMass>\n<PayloadMass>50000</PayloadMass>\n"
                         "<Length>10</Length>\n<AutopilotModule></AutopilotModule>\n"
                         "</Vehicle>\n</Config>\n"));
    write(QStringLiteral("cfg/trains/poezd.xml"),
          QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<Config>\n<Common>\n"
                         "<Title>t</Title>\n</Common>\n"
                         "<Vehicle>\n<ModuleDir>locos</ModuleDir>\n<Module>L</Module>\n"
                         "<ModuleConfigDir>locos</ModuleConfigDir>\n<ModuleConfig>L</ModuleConfig>\n"
                         "<IsOrientationForward>1</IsOrientationForward>\n<Count>1</Count>\n"
                         "<PayloadCoeff>0.0</PayloadCoeff>\n</Vehicle>\n"
                         "<Vehicle>\n<ModuleDir>cars</ModuleDir>\n<Module>W</Module>\n"
                         "<ModuleConfigDir>cars</ModuleConfigDir>\n<ModuleConfig>W</ModuleConfig>\n"
                         "<IsOrientationForward>1</IsOrientationForward>\n<Count>2</Count>\n"
                         "<PayloadCoeff>0.0</PayloadCoeff>\n</Vehicle>\n"
                         "</Config>\n"));

    VehicleCatalog c;
    QVERIFY2(c.load(root), qPrintable(c.errorString()));

    RollingStockPanel panel;
    panel.setCatalog(&c);

    // По умолчанию вкладка «Единицы»
    QCOMPARE(panel.activeTab(), 0);

    // Переключение вкладок
    panel.setActiveTab(1);
    QCOMPARE(panel.activeTab(), 1);
    panel.setActiveTab(0);
    QCOMPARE(panel.activeTab(), 0);

    // selectTrain сам переходит на вкладку «Готовые составы»
    QVERIFY(panel.selectTrain(QStringLiteral("poezd")));
    QCOMPARE(panel.activeTab(), 1);
}

QTEST_MAIN(TestPanel)

#include "test-panel.moc"
