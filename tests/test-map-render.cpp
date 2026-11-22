#include    <QtTest>

#include    <QImage>

#include    "ui/map-widget.h"

using namespace rse;

namespace
{

/// Цвет фона схемы (должен совпадать с MapWidget::paintEvent)
const QRgb BACKGROUND = qRgb(250, 250, 247);

//------------------------------------------------------------------------------
// Число пикселей, отличающихся от фона
//------------------------------------------------------------------------------
int countDrawn(const QImage &img)
{
    int n = 0;

    for (int y = 0; y < img.height(); ++y)
    {
        for (int x = 0; x < img.width(); ++x)
        {
            if (img.pixel(x, y) != BACKGROUND)
            {
                ++n;
            }
        }
    }

    return n;
}

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
class TestMapRender : public QObject
{
    Q_OBJECT

private slots:

    void emptyWidgetShowsHint();
    void overviewDrawsRoute();
    void zoomedViewDrawsArrowsAndNames();
};

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void TestMapRender::emptyWidgetShowsHint()
{
    MapWidget w;
    w.resize(600, 300);

    const QImage img = w.grab().toImage();

    // Подсказка "Откройте каталог маршрута" - немного пикселей, но не пусто
    QVERIFY(countDrawn(img) > 50);
    QVERIFY(countDrawn(img) < 5000);
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void TestMapRender::overviewDrawsRoute()
{
    const QString dir = qEnvironmentVariable("RRS_ROUTE_DIR");

    if (dir.isEmpty())
    {
        QSKIP("RRS_ROUTE_DIR не задан");
    }

    Route route;
    QVERIFY2(route.load(dir), qPrintable(route.errorString()));

    MapWidget w;
    w.resize(1400, 420);
    w.setRoute(&route);

    const QImage img = w.grab().toImage();
    saveShot(img, "overview.png");

    // Весь маршрут на экране: линии путей и подписи станций
    QVERIFY(countDrawn(img) > 1500);
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void TestMapRender::zoomedViewDrawsArrowsAndNames()
{
    const QString dir = qEnvironmentVariable("RRS_ROUTE_DIR");

    if (dir.isEmpty())
    {
        QSKIP("RRS_ROUTE_DIR не задан");
    }

    Route route;
    QVERIFY(route.load(dir));

    const TrajectoryData *p1a = route.find("track_a_p1a");
    QVERIFY(p1a != nullptr);

    MapWidget w;
    w.resize(1400, 520);
    w.setRoute(&route);

    // Крупно: станция A, путь 1А в центре, 0.6 пикселя на метр
    w.viewAt(p1a->pointAt(p1a->length / 2.0), 0.6);

    const QImage img = w.grab().toImage();
    saveShot(img, "zoomed.png");

    QVERIFY(countDrawn(img) > 3000);
}

QTEST_MAIN(TestMapRender)

#include "test-map-render.moc"
