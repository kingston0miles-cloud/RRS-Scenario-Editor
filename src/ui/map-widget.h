#ifndef     RSE_MAP_WIDGET_H
#define     RSE_MAP_WIDGET_H

#include    <QWidget>
#include    <QTimer>
#include    <QTransform>

#include    "route/route.h"
#include    "scenario/pathfinder.h"
#include    "scenario/scenario.h"

namespace rse
{

/// Светофор для подсветки на карте: стрелка, на которой он стоит, и литера
struct SignalMark
{
    QString sw;
    QString liter;

    /// Станционный (входной/маршрутный/выходной) - цветное кольцо, линейный -
    /// точка; подпись литеры ставится только у станционных и линейных с литерой
    bool station = true;

    /// Показать красным: маршрут этого сигнала не строится
    bool problem = false;
};

//------------------------------------------------------------------------------
// Схема путей маршрута: масштаб колесом, перемещение левой кнопкой,
// стрелки показывают направление "1" (от начала траектории к концу)
//------------------------------------------------------------------------------
class MapWidget : public QWidget
{
    Q_OBJECT

public:

    explicit MapWidget(QWidget *parent = nullptr);

    /// Задать маршрут (виджет им не владеет) и подогнать вид
    void setRoute(const Route *route);

    /// Расставленные поезда (виджет ими не владеет)
    void setScenario(const Scenario *scenario);

    /// Режим расстановки: состав-заготовка следует за курсором призраком, левый щелчок
    /// по пути даёт сигнал trackClicked. nullptr - режим выключен
    void setPlacementTemplate(const PlacedTrain *train);

    /// Режим выбора точки: курсор-прицел, левый щелчок по пути даёт trackClicked
    void setPickMode(bool pick);

    bool pickMode() const
    {
        return pick_;
    }

    /// Подсветить путь поезда по расписанию и его станции (пустой путь - убрать).
    /// Конфликтные пути рисуются красным поверх оранжевого маршрута
    void setTripOverlay(const QVector<PathSegment> &path, const QVector<TimetableStation> &stations,
                        const QStringList &conflict_trajectories = {});

    /// Перелёт к середине траектории с кратковременной подсветкой (ссылки в
    /// замечаниях графика и клик по событию)
    void locateTrajectory(const QString &trajectory);

    /// Подсветить светофоры, которые откроются маршрутами графика (пусто - убрать;
    /// из окна графика, этап 19)
    void setSignalMarks(const QVector<SignalMark> &marks);

    bool placementActive() const
    {
        return placement_ != nullptr;
    }

    /// Подсказка текущего режима карты (аудит 1.4): расстановка или выбор точки;
    /// пусто - обычный режим. Тот же текст рисуется цветной полосой сверху
    QString mapModeHint() const;

    /// Выделить поезд (индекс в сценарии, -1 - нет)
    void setSelectedTrain(int index);

    /// Вывести на экран точку траектории
    void centerOn(const QString &trajectory, double coord);

    bool showNames() const
    {
        return show_names_;
    }

    bool showArrows() const
    {
        return show_arrows_;
    }

    bool showSwitches() const
    {
        return show_switches_;
    }

public slots:

    /// Показать весь маршрут целиком
    void fitAll();

    /// Повернуть вид на 90 градусов по часовой стрелке
    void rotateView();

    /// Поместить точку плана в центр виджета с заданным масштабом (пикселей на метр)
    void viewAt(const QPointF &center, double scale);

    void setShowNames(bool show);
    void setShowArrows(bool show);
    void setShowSwitches(bool show);

signals:

    /// Выбранный поезд перетащен мышью: голова брошена на (траектория, координата),
    /// dir - направление головы (на новом пути - по ходу пути)
    void trainDropped(int index, const QString &trajectory, double coord, int dir);

    /// Щелчок по поезду на карте (без перетаскивания)
    void trainClicked(int index);

    /// Левый щелчок (без перетаскивания) по пути в режиме расстановки
    void trackClicked(const rse::Route::Hit &hit);

    /// Курсор над траекторией (hit.valid) или ушёл с неё
    void hoverChanged(const rse::Route::Hit &hit);

    /// Имя траектории скопировано в буфер обмена (правая кнопка)
    void trajectoryCopied(const QString &text);

protected:

    void paintEvent(QPaintEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:

    const Route *route_ = nullptr;
    const Scenario *scenario_ = nullptr;
    const PlacedTrain *placement_ = nullptr;
    int selected_train_ = -1;
    bool pick_ = false;
    bool train_drag_ = false;
    int press_train_ = -1;
    QVector<PathSegment> trip_path_;
    QVector<TimetableStation> trip_stations_;
    QVector<SignalMark> signal_marks_;

    /// Конфликтные пути графика: участки маршрута рисуются красным, не оранжевым
    QStringList trip_conflicts_;

    /// Траектория кратковременной подсветки (locateTrajectory) и её таймер
    QString highlight_traj_;
    QTimer highlight_timer_;

    /// Масштаб: пикселей на метр
    double scale_ = 1.0;

    /// Точка плана в центре виджета
    QPointF center_;

    /// Число поворотов вида на 90 градусов по часовой стрелке (0..3)
    int rotation_ = 0;

    bool show_names_ = true;
    bool show_arrows_ = true;
    bool show_switches_ = true;

    bool dragging_ = false;
    bool moved_ = false;
    QPoint last_pos_;
    QPoint press_pos_;

    Route::Hit hover_;

    QTransform worldToScreen() const;

    void updateHover(const QPoint &pos);

    /// Индекс поезда под точкой экрана (-1 - нет); prefer - проверить его первым
    int trainAt(const QPoint &pos, int prefer) const;

    /// Состав в положении, куда его сейчас тащат (valid - курсор над путём)
    PlacedTrain dragGhost(bool *valid) const;

    /// Стрелки вдоль полилинии; neutral - вместо стрелок точки (направление неизвестно)
    void drawArrows(QPainter &p, const QPolygonF &poly, bool neutral = false) const;

    void drawFootprint(QPainter &p, const QTransform &t, const Footprint &fp,
                       const QColor &color, const QString &label, int dir, bool ghost) const;
};

} // namespace rse

#endif // RSE_MAP_WIDGET_H
