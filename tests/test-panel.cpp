#include    <QtTest>

#include    <QImage>

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

QTEST_MAIN(TestPanel)

#include "test-panel.moc"
