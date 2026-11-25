#ifndef     RSE_ROUTE_H
#define     RSE_ROUTE_H

#include    "route/route-data.h"

#include    <QHash>
#include    <QMap>
#include    <QString>
#include    <QStringList>
#include    <QVector>

namespace rse
{

//------------------------------------------------------------------------------
// Маршрут RRS: траектории, стрелки, станции и путевые точки.
// Читает каталог routes/<имя_маршрута> (подкаталог topology).
//------------------------------------------------------------------------------
class Route
{
public:

    /// Результат поиска ближайшей траектории к точке плана
    struct Hit
    {
        bool valid = false;
        QString trajectory;
        /// Траекторная координата проекции точки, м
        double coord = 0.0;
        /// Расстояние от точки до траектории в плане, м
        double distance = 0.0;
        /// Проекция точки на траекторию
        QPointF pos;
    };

    /// Конец траектории, к которому примыкает следующая траектория через стрелку
    struct Endpoint
    {
        QString trajectory;
        /// true - примыкает концом траектории, false - началом
        bool atEnd = false;
        /// Положение стрелки, в которое переведён этот вариант: "плюс"
        bool plus = true;
        QString switchName;
    };

    /// Что находится за концом траектории (atEnd - конец, иначе начало).
    /// Пустой список - тупик. Больше одного элемента - положение стрелки
    /// определяет симулятор в момент движения, однозначно угадать нельзя
    QVector<Endpoint> crossings(const QString &trajectory, bool at_end) const;

    /// Правильное направление движения по траектории, выведенное из светофоров:
    /// +1 «туда» (вдоль траектории, от начала к концу), -1 «обратно» (от конца к началу),
    /// 0 - не определено (станционные пути, где светофоры есть в обе стороны).
    /// На неправильном пути поездные светофоры по ходу поезда не открываются.
    /// Светофор в стрелке относится к одному направлению, если у стрелки задана
    /// только одна из литер (SignalLiterFwd или SignalLiterBwd); найденное
    /// направление переносится по цепочке траекторий через простые стыки
    int orientation(const QString &trajectory) const;

    /// Загрузить маршрут из каталога. При ошибке возвращает false,
    /// причина в errorString()
    bool load(const QString &route_dir);

    void clear();

    bool isLoaded() const
    {
        return !trajectories_.isEmpty();
    }

    const QString &dir() const
    {
        return dir_;
    }

    /// Название маршрута (из description.xml, иначе имя каталога)
    const QString &title() const
    {
        return title_;
    }

    const QString &errorString() const
    {
        return error_;
    }

    /// Некритичные замечания, найденные при загрузке
    const QStringList &warnings() const
    {
        return warnings_;
    }

    const QMap<QString, TrajectoryData> &trajectories() const
    {
        return trajectories_;
    }

    const TrajectoryData *find(const QString &name) const;

    /// Стрелка по имени (nullptr, если нет)
    const SwitchData *findSwitch(const QString &name) const;

    const QVector<SwitchData> &switches() const
    {
        return switches_;
    }

    const QVector<StationData> &stations() const
    {
        return stations_;
    }

    const QVector<WaypointData> &waypoints() const
    {
        return waypoints_;
    }

    /// Габарит всего маршрута в плане
    const QRectF &bounds() const
    {
        return bounds_;
    }

    /// Ближайшая к точке плана траектория в пределах max_distance (м)
    Hit nearest(const QPointF &world, double max_distance) const;

private:

    QString dir_;
    QString title_;
    QString error_;
    QStringList warnings_;

    QMap<QString, TrajectoryData> trajectories_;
    QVector<SwitchData> switches_;
    QVector<StationData> stations_;
    QVector<WaypointData> waypoints_;
    QRectF bounds_;

    void loadTitle();
    void loadSwitches();
    void loadStations();
    void loadWaypoints();
    void loadSpeedMaps();

    /// Вычислить положение узлов-стрелок и проверить ссылки на траектории
    void resolveSwitches();

    /// Проверить, что путевые точки ссылаются на существующие траектории
    void checkWaypoints();

    void calcBounds();

    /// Индекс «конец траектории → стрелки» для быстрого поиска переходов
    static QString endKey(const QString &trajectory, bool at_end);

    void buildIndex();

    /// Вывести правильные направления путей по светофорам (для orientation)
    void calcPathDirections();

    QHash<QString, int> path_dir_;

    /// Ключ "имя\x01e" (конец) / "имя\x01s" (начало) → индексы стрелок
    QHash<QString, QVector<int>> endpoint_index_;
    mutable QHash<QString, int> switch_by_name_;
};

} // namespace rse

#endif // RSE_ROUTE_H
