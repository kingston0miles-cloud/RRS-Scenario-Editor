#include    "ui/map-widget.h"

#include    <QClipboard>
#include    <QGuiApplication>
#include    <QMouseEvent>
#include    <QPainter>
#include    <QWheelEvent>

#include    <algorithm>
#include    <cmath>

namespace rse
{

namespace
{

constexpr double MIN_SCALE = 0.002;
constexpr double MAX_SCALE = 40.0;

/// Шаг масштабирования на один щелчок колеса
constexpr double ZOOM_STEP = 1.2;

/// Радиус захвата траектории курсором, пикселей
constexpr double HOVER_RADIUS_PX = 10.0;

/// Расстояние между стрелками направления, пикселей
constexpr double ARROW_SPACING_PX = 90.0;

/// Минимальный масштаб, с которого рисуются узлы стрелок и их имена
constexpr double SWITCH_DOT_SCALE = 0.08;
constexpr double SWITCH_NAME_SCALE = 0.6;

//------------------------------------------------------------------------------
// Прямоугольник подписи. anchor.y() - нижняя граница текста, по горизонтали
// подпись центрируется на anchor.x() либо прижимается влево (AlignLeft)
// или вправо (AlignRight)
//------------------------------------------------------------------------------
QRectF labelRect(const QFontMetricsF &fm, const QPointF &anchor,
                 const QString &text, Qt::Alignment align = Qt::AlignHCenter)
{
    const QRectF tr = fm.boundingRect(text);

    double x = anchor.x() - tr.width() / 2.0;

    if (align & Qt::AlignLeft)
    {
        x = anchor.x();
    }
    else if (align & Qt::AlignRight)
    {
        x = anchor.x() - tr.width();
    }

    return QRectF(x - 2.0, anchor.y() - tr.height(), tr.width() + 4.0, tr.height() + 2.0);
}

//------------------------------------------------------------------------------
// Подпись с полупрозрачной подложкой в заранее вычисленном прямоугольнике
//------------------------------------------------------------------------------
void drawLabelRect(QPainter &p, const QRectF &bg, const QString &text, const QColor &color)
{
    const QFontMetricsF fm(p.font());

    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 255, 255, 190));
    p.drawRoundedRect(bg, 2.0, 2.0);

    p.setPen(color);
    p.drawText(QPointF(bg.left() + 2.0, bg.bottom() - 2.0 - fm.descent()), text);
}

//------------------------------------------------------------------------------
// Пересекается ли прямоугольник с уже размещёнными подписями
//------------------------------------------------------------------------------
bool collides(const QVector<QRectF> &placed, const QRectF &r)
{
    for (const QRectF &q : placed)
    {
        if (q.intersects(r))
        {
            return true;
        }
    }

    return false;
}

} // namespace

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
MapWidget::MapWidget(QWidget *parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setMinimumSize(320, 240);
    setAutoFillBackground(false);
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void MapWidget::setRoute(const Route *route)
{
    route_ = route;
    hover_ = Route::Hit();

    rotation_ = 0;

    // Протяжённые маршруты показываем горизонтально
    if (route_ != nullptr && route_->isLoaded())
    {
        const QRectF b = route_->bounds();

        if (b.height() > b.width() * 1.5)
        {
            rotation_ = 1;
        }
    }

    fitAll();
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
QTransform MapWidget::worldToScreen() const
{
    // Операции QTransform применяются к точке в обратном порядке:
    // сдвиг к центру -> масштаб с переворотом оси Y -> поворот -> сдвиг в центр виджета
    QTransform t;
    t.translate(width() / 2.0, height() / 2.0);
    t.rotate(rotation_ * 90.0);
    t.scale(scale_, -scale_);
    t.translate(-center_.x(), -center_.y());

    return t;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void MapWidget::fitAll()
{
    if (route_ == nullptr || !route_->isLoaded())
    {
        update();
        return;
    }

    const QRectF b = route_->bounds();

    double w = std::max(b.width(), 1.0);
    double h = std::max(b.height(), 1.0);

    if (rotation_ % 2 != 0)
    {
        std::swap(w, h);
    }

    const double avail_w = std::max(width() - 100.0, 50.0);
    const double avail_h = std::max(height() - 100.0, 50.0);

    scale_ = std::clamp(std::min(avail_w / w, avail_h / h), MIN_SCALE, MAX_SCALE);
    center_ = b.center();

    update();
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void MapWidget::rotateView()
{
    rotation_ = (rotation_ + 1) % 4;
    update();
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void MapWidget::viewAt(const QPointF &center, double scale)
{
    center_ = center;
    scale_ = std::clamp(scale, MIN_SCALE, MAX_SCALE);
    update();
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void MapWidget::setScenario(const Scenario *scenario)
{
    scenario_ = scenario;
    update();
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void MapWidget::setPlacementTemplate(const PlacedTrain *train)
{
    placement_ = train;
    setCursor((train != nullptr || pick_) ? Qt::CrossCursor : Qt::ArrowCursor);
    update();
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void MapWidget::setPickMode(bool pick)
{
    pick_ = pick;
    setCursor((pick || placement_ != nullptr) ? Qt::CrossCursor : Qt::ArrowCursor);
    update();
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void MapWidget::setTripOverlay(const QVector<PathSegment> &path,
                               const QVector<TimetableStation> &stations)
{
    trip_path_ = path;
    trip_stations_ = stations;
    update();
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void MapWidget::setSelectedTrain(int index)
{
    selected_train_ = index;
    update();
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void MapWidget::centerOn(const QString &trajectory, double coord)
{
    if (route_ == nullptr)
    {
        return;
    }

    const TrajectoryData *traj = route_->find(trajectory);

    if (traj != nullptr)
    {
        center_ = traj->pointAt(coord);
        update();
    }
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void MapWidget::setShowNames(bool show)
{
    show_names_ = show;
    update();
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void MapWidget::setShowArrows(bool show)
{
    show_arrows_ = show;
    update();
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void MapWidget::setShowSwitches(bool show)
{
    show_switches_ = show;
    update();
}

//------------------------------------------------------------------------------
// Стрелки направления вдоль полилинии (экранные координаты)
//------------------------------------------------------------------------------
void MapWidget::drawArrows(QPainter &p, const QPolygonF &poly, bool neutral) const
{
    if (poly.size() < 2)
    {
        return;
    }

    const QRectF visible = QRectF(rect()).adjusted(-12.0, -12.0, 12.0, 12.0);

    double next_at = ARROW_SPACING_PX / 2.0;
    double passed = 0.0;

    for (int i = 0; i + 1 < poly.size(); ++i)
    {
        const QPointF a = poly[i];
        const QPointF d = poly[i + 1] - a;
        const double seg = std::hypot(d.x(), d.y());

        if (seg <= 0.0)
        {
            continue;
        }

        const QPointF dir(d.x() / seg, d.y() / seg);
        const QPointF nrm(-dir.y(), dir.x());

        while (next_at <= passed + seg)
        {
            const QPointF c = a + dir * (next_at - passed);

            if (visible.contains(c))
            {
                if (neutral)
                {
                    p.drawEllipse(c, 2.0, 2.0);
                }
                else
                {
                    const QPointF tip = c + dir * 5.0;
                    const QPointF b1 = c - dir * 4.0 + nrm * 3.5;
                    const QPointF b2 = c - dir * 4.0 - nrm * 3.5;

                    p.drawPolygon(QPolygonF() << tip << b1 << b2);
                }
            }

            next_at += ARROW_SPACING_PX;
        }

        passed += seg;
    }
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void MapWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    p.fillRect(rect(), QColor(250, 250, 247));

    if (route_ == nullptr || !route_->isLoaded())
    {
        p.setPen(QColor(120, 120, 120));
        p.drawText(rect(), Qt::AlignCenter,
                   tr("Откройте каталог маршрута: Файл → Открыть маршрут…"));
        return;
    }

    const QTransform t = worldToScreen();

    bool ok = false;
    const QTransform inv = t.inverted(&ok);

    if (!ok)
    {
        return;
    }

    // Видимая область в координатах плана (с запасом)
    const QRectF view_world = inv.mapRect(QRectF(rect())).adjusted(-1.0, -1.0, 1.0, 1.0);

    const QColor track_color(70, 90, 125);
    const QColor hover_color(235, 120, 20);

    QPolygonF hover_poly;

    // Траектории
    p.setBrush(Qt::NoBrush);

    for (const TrajectoryData &traj : route_->trajectories())
    {
        if (!traj.bounds.adjusted(-1.0, -1.0, 1.0, 1.0).intersects(view_world))
        {
            continue;
        }

        QPolygonF poly;
        poly.reserve(traj.points.size());

        for (const QPointF &pt : traj.points)
        {
            poly << t.map(pt);
        }

        if (hover_.valid && traj.name == hover_.trajectory)
        {
            hover_poly = poly;
            continue;
        }

        p.setPen(QPen(track_color, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPolyline(poly);

        if (show_arrows_)
        {
            // Стрелки по правильному направлению пути (по светофорам): «туда» - синие
            // вдоль траектории, «обратно» - оранжевые против неё; направление
            // неизвестно - серые точки
            const int o = route_->orientation(traj.name);

            p.setPen(Qt::NoPen);
            p.setBrush(o < 0 ? QColor(215, 105, 15)
                             : (o > 0 ? QColor(25, 90, 210) : QColor(150, 155, 165)));

            if (o < 0)
            {
                QPolygonF rev;

                for (int i = poly.size() - 1; i >= 0; --i)
                {
                    rev << poly[i];
                }

                drawArrows(p, rev);
            }
            else
            {
                drawArrows(p, poly, o == 0);
            }

            p.setBrush(Qt::NoBrush);
        }
    }

    // Траектория под курсором рисуется поверх остальных
    if (!hover_poly.isEmpty())
    {
        p.setPen(QPen(hover_color, 3.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPolyline(hover_poly);

        p.setPen(Qt::NoPen);
        p.setBrush(hover_color.darker(120));

        const int ho = route_->orientation(hover_.trajectory);

        if (ho < 0)
        {
            QPolygonF rev;

            for (int i = hover_poly.size() - 1; i >= 0; --i)
            {
                rev << hover_poly[i];
            }

            drawArrows(p, rev);
        }
        else
        {
            drawArrows(p, hover_poly, ho == 0);
        }

        p.setBrush(Qt::NoBrush);
    }

    QFont small_font = font();
    small_font.setPointSizeF(std::max(7.0, font().pointSizeF() - 1.5));

    // Подписи ставятся по приоритету: станции, имена траекторий, имена стрелок.
    // Подпись, перекрывающая уже поставленную, пропускается
    QVector<QRectF> placed;

    // Станции: прямоугольники подписей резервируем заранее, рисуем в конце.
    // Подпись у правого края окна переносится влево от значка
    QFont station_font = font();
    station_font.setBold(true);

    struct StationLabel
    {
        QPointF pos;
        QRectF rect;
        const StationData *station;
    };

    QVector<StationLabel> station_labels;

    {
        const QFontMetricsF sfm(station_font);

        for (const StationData &st : route_->stations())
        {
            const QPointF sp = t.map(st.pos);

            if (!QRectF(rect()).adjusted(-80, -30, 80, 30).contains(sp))
            {
                continue;
            }

            QRectF r = labelRect(sfm, sp + QPointF(10.0, -8.0), st.name, Qt::AlignLeft);

            if (r.right() > width() - 4.0)
            {
                r = labelRect(sfm, sp + QPointF(-10.0, -8.0), st.name, Qt::AlignRight);
            }

            placed.push_back(r);
            station_labels.push_back({ sp, r, &st });
        }
    }

    // Узлы стрелок: точки сразу, подписи - после имён траекторий
    QVector<QPair<QPointF, QString>> switch_labels;

    if (show_switches_ && scale_ >= SWITCH_DOT_SCALE)
    {
        const bool names = show_names_ && scale_ >= SWITCH_NAME_SCALE;

        for (const SwitchData &sw : route_->switches())
        {
            if (!sw.hasPos || !view_world.contains(sw.pos))
            {
                continue;
            }

            const QPointF sp = t.map(sw.pos);

            p.setPen(Qt::NoPen);
            p.setBrush(QColor(170, 40, 40));
            p.drawEllipse(sp, 3.0, 3.0);

            if (names)
            {
                switch_labels.push_back({ sp + QPointF(0.0, 14.0), sw.name });
            }
        }

        p.setBrush(Qt::NoBrush);
    }

    // Имена траекторий: рисуем, если траектория на экране заметно длиннее подписи
    if (show_names_)
    {
        p.setFont(small_font);

        const QFontMetricsF fm(small_font);

        for (const TrajectoryData &traj : route_->trajectories())
        {
            if (!traj.bounds.adjusted(-1.0, -1.0, 1.0, 1.0).intersects(view_world))
            {
                continue;
            }

            const double screen_len = traj.length * scale_;

            if (screen_len < fm.horizontalAdvance(traj.name) * 1.3)
            {
                continue;
            }

            const QPointF mid = t.map(traj.pointAt(traj.length / 2.0));

            if (!QRectF(rect()).adjusted(-40, -20, 40, 20).contains(mid))
            {
                continue;
            }

            const QRectF r = labelRect(fm, mid - QPointF(0.0, 5.0), traj.name);

            if (collides(placed, r))
            {
                continue;
            }

            placed.push_back(r);
            drawLabelRect(p, r, traj.name, QColor(60, 70, 90));
        }
    }

    // Имена стрелок
    if (!switch_labels.isEmpty())
    {
        p.setFont(small_font);

        const QFontMetricsF fm(small_font);

        for (const auto &sl : std::as_const(switch_labels))
        {
            const QRectF r = labelRect(fm, sl.first, sl.second);

            if (collides(placed, r))
            {
                continue;
            }

            placed.push_back(r);
            drawLabelRect(p, r, sl.second, QColor(150, 40, 40));
        }
    }

    // Станции (прямоугольники подписей зарезервированы выше)
    p.setFont(station_font);

    for (const StationLabel &sl : std::as_const(station_labels))
    {
        const QPointF sp = sl.pos;

        p.setPen(QPen(QColor(30, 110, 60), 1.5));
        p.setBrush(QColor(60, 170, 100));
        p.drawPolygon(QPolygonF() << sp + QPointF(0, -6) << sp + QPointF(6, 0)
                                  << sp + QPointF(0, 6) << sp + QPointF(-6, 0));

        drawLabelRect(p, sl.rect, sl.station->name, QColor(20, 90, 45));
    }

    // Поставленные поезда
    if (scenario_ != nullptr)
    {
        p.setFont(small_font);

        for (int i = 0; i < scenario_->size(); ++i)
        {
            const PlacedTrain &tr = scenario_->trains()[i];
            const Footprint fp = computeFootprint(*route_, tr.trajectory, tr.coord,
                                                  tr.dir, tr.length);

            const bool sel = (i == selected_train_);

            drawFootprint(p, t, fp, sel ? QColor(200, 30, 120) : QColor(30, 130, 90),
                          tr.name, tr.dir, false);
        }
    }

    // Путь поезда по расписанию
    if (!trip_path_.isEmpty())
    {
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor(235, 140, 20, 150), 7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));

        for (const PathSegment &seg : std::as_const(trip_path_))
        {
            const TrajectoryData *td = route_->find(seg.trajectory);

            if (td == nullptr)
            {
                continue;
            }

            const QVector<QPointF> pts = td->subPolyline(std::min(seg.from, seg.to),
                                                         std::max(seg.from, seg.to));
            QPolygonF poly;

            for (const QPointF &pt : pts)
            {
                poly << t.map(pt);
            }

            p.drawPolyline(poly);
        }

        p.setFont(small_font);

        for (const TimetableStation &st : std::as_const(trip_stations_))
        {
            const TrajectoryData *td = route_->find(st.targetTrajectory);

            if (td == nullptr)
            {
                continue;
            }

            const QPointF sp = t.map(td->pointAt(st.targetCoord));

            p.setPen(QPen(QColor(150, 70, 0), 1.5));
            p.setBrush(st.stops() ? QColor(235, 140, 20) : Qt::white);
            p.drawEllipse(sp, 5.5, 5.5);
            p.drawText(sp + QPointF(8, -6), st.name);
        }
    }

    // Призрак перетаскиваемого поезда
    {
        bool valid = false;
        const PlacedTrain g = dragGhost(&valid);

        if (valid)
        {
            const PlacementCheck chk = scenario_->check(*route_, g, selected_train_);

            p.setFont(small_font);
            drawFootprint(p, t, chk.footprint,
                          chk.hasProblems() ? QColor(210, 40, 40) : QColor(40, 110, 220),
                          g.name, g.dir, true);
        }
    }

    // Призрак состава под курсором в режиме расстановки
    if (placement_ != nullptr && hover_.valid && scenario_ != nullptr)
    {
        PlacedTrain ghost = *placement_;
        ghost.trajectory = hover_.trajectory;
        ghost.coord = hover_.coord;
        ghost.dir = (route_->orientation(ghost.trajectory) != 0) ? route_->orientation(ghost.trajectory) : 1;

        const PlacementCheck chk = scenario_->check(*route_, ghost);

        p.setFont(small_font);
        drawFootprint(p, t, chk.footprint,
                      chk.hasProblems() ? QColor(210, 40, 40) : QColor(40, 110, 220),
                      ghost.title, ghost.dir, true);
    }

    // Точка проекции курсора на траекторию
    if (hover_.valid)
    {
        const QPointF hp = t.map(hover_.pos);

        p.setPen(QPen(hover_color.darker(130), 1.5));
        p.setBrush(Qt::white);
        p.drawEllipse(hp, 4.5, 4.5);
    }
}

//------------------------------------------------------------------------------
// Состав: толстая линия по занятым участкам, треугольник на голове, подпись
//------------------------------------------------------------------------------
void MapWidget::drawFootprint(QPainter &p, const QTransform &t, const Footprint &fp,
                              const QColor &color, const QString &label, int dir,
                              bool ghost) const
{
    if (fp.isEmpty())
    {
        return;
    }

    QColor c = color;

    if (ghost)
    {
        c.setAlpha(150);
    }

    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(c, 6.0, Qt::SolidLine, Qt::FlatCap, Qt::RoundJoin));

    for (const FootprintSegment &seg : fp.segments)
    {
        const TrajectoryData *traj = route_->find(seg.trajectory);

        if (traj == nullptr)
        {
            continue;
        }

        QPolygonF poly;

        for (const QPointF &pt : traj->subPolyline(seg.from, seg.to))
        {
            poly << t.map(pt);
        }

        p.drawPolyline(poly);
    }

    // Голова - первый участок следа; её положение и касательная
    const FootprintSegment &head = fp.segments.first();
    const TrajectoryData *ht = route_->find(head.trajectory);

    if (ht == nullptr)
    {
        return;
    }

    QPointF tangent;
    const double head_coord = (dir >= 0) ? head.to : head.from;
    const QPointF hp = t.map(ht->pointAt(head_coord, &tangent));

    // Касательная - в координатах плана; переводим в экранное направление
    const QPointF tip_world = ht->pointAt(head_coord) + tangent * 0.01;
    QPointF sd = t.map(tip_world) - hp;
    const double sl = std::hypot(sd.x(), sd.y());

    if (sl > 0.0)
    {
        sd /= sl;

        if (dir < 0)
        {
            sd = -sd;
        }

        const QPointF n(-sd.y(), sd.x());

        p.setPen(QPen(Qt::white, 1.0));
        p.setBrush(c.darker(130));
        p.drawPolygon(QPolygonF() << hp + sd * 9.0 << hp - sd * 2.0 + n * 6.0
                                  << hp - sd * 2.0 - n * 6.0);
    }

    if (!label.isEmpty())
    {
        const QFontMetricsF fm(p.font());
        const QRectF r = labelRect(fm, hp + QPointF(0.0, -10.0), label);

        drawLabelRect(p, r, label, c.darker(150));
    }
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void MapWidget::wheelEvent(QWheelEvent *event)
{
    if (route_ == nullptr || !route_->isLoaded())
    {
        return;
    }

    const int delta = event->angleDelta().y();

    if (delta == 0)
    {
        return;
    }

    bool ok = false;
    const QPointF cursor = event->position();
    const QPointF before = worldToScreen().inverted(&ok).map(cursor);

    if (!ok)
    {
        return;
    }

    const double factor = std::pow(ZOOM_STEP, delta / 120.0);
    scale_ = std::clamp(scale_ * factor, MIN_SCALE, MAX_SCALE);

    // Точка под курсором должна остаться на месте
    const QPointF after = worldToScreen().inverted(&ok).map(cursor);

    if (ok)
    {
        center_ += before - after;
    }

    updateHover(event->position().toPoint());
    update();
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
static double distToSegment(const QPointF &p, const QPointF &a, const QPointF &b)
{
    const QPointF ab = b - a;
    const double len2 = ab.x() * ab.x() + ab.y() * ab.y();
    double t = 0.0;

    if (len2 > 1e-12)
    {
        t = ((p.x() - a.x()) * ab.x() + (p.y() - a.y()) * ab.y()) / len2;
        t = std::clamp(t, 0.0, 1.0);
    }

    const QPointF q = a + ab * t;
    const QPointF d = p - q;

    return std::sqrt(d.x() * d.x() + d.y() * d.y());
}

int MapWidget::trainAt(const QPoint &pos, int prefer) const
{
    if (scenario_ == nullptr || route_ == nullptr)
    {
        return -1;
    }

    const QTransform t = worldToScreen();
    int best = -1;
    double best_d = HOVER_RADIUS_PX;

    for (int i = 0; i < scenario_->size(); ++i)
    {
        const PlacedTrain &tr = scenario_->trains()[i];
        const Footprint fp = computeFootprint(*route_, tr.trajectory, tr.coord, tr.dir, tr.length);

        for (const FootprintSegment &seg : fp.segments)
        {
            const TrajectoryData *traj = route_->find(seg.trajectory);

            if (traj == nullptr)
            {
                continue;
            }

            const QVector<QPointF> pts = traj->subPolyline(seg.from, seg.to);

            for (int j = 0; j + 1 < pts.size(); ++j)
            {
                const double d = distToSegment(QPointF(pos), t.map(pts[j]), t.map(pts[j + 1]));

                // выбранный поезд имеет приоритет при близких расстояниях
                const double score = (i == prefer) ? d - 3.0 : d;

                if (score < best_d)
                {
                    best_d = score;
                    best = i;
                }
            }
        }
    }

    return best;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
PlacedTrain MapWidget::dragGhost(bool *valid) const
{
    *valid = false;

    if (!train_drag_ || selected_train_ < 0 || selected_train_ >= scenario_->size() || !hover_.valid)
    {
        return PlacedTrain();
    }

    PlacedTrain g = scenario_->trains()[selected_train_];

    if (hover_.trajectory != g.trajectory)
    {
        const int o = route_->orientation(hover_.trajectory);
        g.dir = (o != 0) ? o : 1;
    }

    g.trajectory = hover_.trajectory;
    g.coord = hover_.coord;
    *valid = true;

    return g;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void MapWidget::mousePressEvent(QMouseEvent *event)
{
    press_train_ = -1;

    // Выбранный поезд тянем мышью; щелчок по другому - выбирает его
    if (event->button() == Qt::LeftButton && placement_ == nullptr && !pick_)
    {
        const int hit = trainAt(event->pos(), selected_train_);

        if (hit >= 0 && hit == selected_train_)
        {
            train_drag_ = true;
            dragging_ = true;
            moved_ = false;
            press_pos_ = event->pos();
            last_pos_ = event->pos();
            setCursor(Qt::SizeAllCursor);
            update();
            return;
        }

        press_train_ = hit;
    }

    if (event->button() == Qt::LeftButton || event->button() == Qt::MiddleButton)
    {
        dragging_ = true;
        moved_ = false;
        last_pos_ = event->pos();
        press_pos_ = event->pos();
        setCursor(Qt::ClosedHandCursor);
    }
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void MapWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (train_drag_)
    {
        if ((event->pos() - press_pos_).manhattanLength() > 3)
        {
            moved_ = true;
        }

        updateHover(event->pos());
        update();
        return;
    }

    if (dragging_)
    {
        bool ok = false;
        const QTransform inv = worldToScreen().inverted(&ok);

        if (ok)
        {
            center_ += inv.map(QPointF(last_pos_)) - inv.map(QPointF(event->pos()));
        }

        last_pos_ = event->pos();

        if ((event->pos() - press_pos_).manhattanLength() > 3)
        {
            moved_ = true;
        }

        update();
        return;
    }

    updateHover(event->pos());
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void MapWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (train_drag_ && event->button() == Qt::LeftButton)
    {
        bool valid = false;
        const PlacedTrain g = dragGhost(&valid);
        const int index = selected_train_;

        train_drag_ = false;
        dragging_ = false;
        unsetCursor();
        update();

        if (moved_ && valid && index >= 0)
        {
            emit trainDropped(index, g.trajectory, g.coord, g.dir);
        }

        return;
    }

    if (event->button() == Qt::LeftButton && press_train_ >= 0 && dragging_ && !moved_)
    {
        const int idx = press_train_;

        press_train_ = -1;
        dragging_ = false;
        unsetCursor();
        emit trainClicked(idx);

        return;
    }

    if (dragging_ && (event->button() == Qt::LeftButton || event->button() == Qt::MiddleButton))
    {
        dragging_ = false;

        if (placement_ != nullptr || pick_)
        {
            setCursor(Qt::CrossCursor);
        }
        else
        {
            unsetCursor();
        }

        updateHover(event->pos());

        // Щелчок без перетаскивания по пути в режиме расстановки
        if (!moved_ && event->button() == Qt::LeftButton
            && (placement_ != nullptr || pick_) && hover_.valid)
        {
            emit trackClicked(hover_);
        }

        return;
    }

    // Правая кнопка: скопировать имя траектории под курсором (как в карте игры)
    if (event->button() == Qt::RightButton && hover_.valid)
    {
        const QString text = hover_.trajectory;

        QGuiApplication::clipboard()->setText(text);
        emit trajectoryCopied(text);
    }
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void MapWidget::leaveEvent(QEvent *)
{
    if (hover_.valid)
    {
        hover_ = Route::Hit();
        emit hoverChanged(hover_);
        update();
    }
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void MapWidget::resizeEvent(QResizeEvent *)
{
    update();
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void MapWidget::updateHover(const QPoint &pos)
{
    if (route_ == nullptr || !route_->isLoaded())
    {
        return;
    }

    bool ok = false;
    const QTransform inv = worldToScreen().inverted(&ok);

    if (!ok)
    {
        return;
    }

    const QPointF world = inv.map(QPointF(pos));
    const Route::Hit hit = route_->nearest(world, HOVER_RADIUS_PX / scale_);

    const bool changed = (hit.valid != hover_.valid)
                      || (hit.valid && (hit.trajectory != hover_.trajectory
                                        || std::abs(hit.coord - hover_.coord) > 0.05));

    hover_ = hit;

    if (changed)
    {
        emit hoverChanged(hover_);
        update();
    }
    else if (placement_ != nullptr || pick_)
    {
        update();
    }
}

} // namespace rse
