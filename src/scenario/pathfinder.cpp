#include    "scenario/pathfinder.h"

#include    <QHash>

#include    <algorithm>
#include    <functional>
#include    <queue>

namespace rse
{

namespace
{

/// Состояние поиска: траектория и направление движения по ней
struct State
{
    QString trajectory;
    int dir = 1;
    /// Координата, с которой поезд вступил на траекторию
    double entry = 0.0;

    QString key() const
    {
        return trajectory + (dir > 0 ? QStringLiteral("|+") : QStringLiteral("|-"));
    }
};

struct QueueItem
{
    double cost;
    QString key;

    bool operator>(const QueueItem &o) const
    {
        return cost > o.cost;
    }
};

struct Node
{
    State state;
    double cost = 0.0;
    QString parent;
    QString via;   // имя стрелки, через которую вошли
};

//------------------------------------------------------------------------------
// Достижима ли цель с входа entry в траекторию в направлении dir
//------------------------------------------------------------------------------
bool goalReachable(double entry, int dir, double goal)
{
    return (dir > 0) ? (goal >= entry - 1e-6) : (goal <= entry + 1e-6);
}

} // namespace

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
PathResult findPath(const Route &route, const QString &traj0, double coord0, int dir0,
                    const QString &traj1, double coord1, double wrong_way_penalty)
{
    PathResult res;

    const TrajectoryData *t0 = route.find(traj0);
    const TrajectoryData *t1 = route.find(traj1);

    if (t0 == nullptr || t1 == nullptr)
    {
        res.error = QStringLiteral("Начальная или конечная траектория не найдена в маршруте.");
        return res;
    }

    coord0 = std::clamp(coord0, 0.0, t0->length);
    coord1 = std::clamp(coord1, 0.0, t1->length);

    auto penalty = [&](const QString &traj, int dir)
    {
        const int o = route.orientation(traj);
        return (o != 0 && o != dir) ? wrong_way_penalty : 0.0;
    };

    QHash<QString, Node> nodes;
    std::priority_queue<QueueItem, std::vector<QueueItem>, std::greater<QueueItem>> queue;

    // Цель на той же траектории впереди по ходу
    if (traj0 == traj1 && goalReachable(coord0, dir0, coord1)
        && ((dir0 > 0) ? coord1 >= coord0 : coord1 <= coord0))
    {
        PathSegment seg;
        seg.trajectory = traj0;
        seg.from = coord0;
        seg.to = coord1;
        seg.dir = dir0;

        res.found = true;
        res.segments.push_back(seg);
        res.length = seg.length();
        res.wrongWay = (route.orientation(traj0) != 0 && route.orientation(traj0) != dir0);

        return res;
    }

    State start;
    start.trajectory = traj0;
    start.dir = dir0 >= 0 ? 1 : -1;
    start.entry = coord0;

    Node n0;
    n0.state = start;
    n0.cost = 0.0;
    nodes.insert(start.key(), n0);
    queue.push({ 0.0, start.key() });

    QHash<QString, double> done;
    QString goal_key;
    double best_goal = -1.0;

    while (!queue.empty())
    {
        const QueueItem item = queue.top();
        queue.pop();

        if (done.contains(item.key))
        {
            continue;
        }

        done.insert(item.key, item.cost);

        if (best_goal >= 0.0 && item.cost >= best_goal)
        {
            break;
        }

        const Node cur = nodes.value(item.key);
        const TrajectoryData *traj = route.find(cur.state.trajectory);

        // Цель на этой траектории (кроме стартовой, обработанной выше)
        if (cur.state.trajectory == traj1 && cur.state.key() != start.key()
            && goalReachable(cur.state.entry, cur.state.dir, coord1))
        {
            const double total = cur.cost + std::abs(coord1 - cur.state.entry)
                               + penalty(traj1, cur.state.dir);

            if (best_goal < 0.0 || total < best_goal)
            {
                best_goal = total;
                goal_key = cur.state.key();
            }
        }

        // Переход за конец траектории
        const double remaining = (cur.state.dir > 0) ? traj->length - cur.state.entry
                                                      : cur.state.entry;
        const double base = cur.cost + remaining + penalty(cur.state.trajectory, cur.state.dir);

        const QVector<Route::Endpoint> next = route.crossings(cur.state.trajectory, cur.state.dir > 0);

        for (const Route::Endpoint &e : next)
        {
            const TrajectoryData *nt = route.find(e.trajectory);

            State s;
            s.trajectory = e.trajectory;
            s.dir = e.atEnd ? -1 : 1;
            s.entry = e.atEnd ? nt->length : 0.0;

            if (done.contains(s.key()))
            {
                continue;
            }

            auto it = nodes.find(s.key());

            if (it == nodes.end() || base < it->cost)
            {
                Node n;
                n.state = s;
                n.cost = base;
                n.parent = cur.state.key();
                n.via = e.switchName;
                nodes.insert(s.key(), n);
                queue.push({ base, s.key() });
            }
        }
    }

    if (goal_key.isEmpty())
    {
        res.error = QStringLiteral("Путь не найден: от начальной точки в выбранном направлении "
                                   "до конечной проехать нельзя (поезд не разворачивается).");
        return res;
    }

    // Восстановление пути от цели к началу
    QVector<Node> chain;

    for (QString k = goal_key; !k.isEmpty(); k = nodes.value(k).parent)
    {
        chain.push_back(nodes.value(k));
    }

    std::reverse(chain.begin(), chain.end());

    for (int i = 0; i < chain.size(); ++i)
    {
        const Node &n = chain[i];
        const TrajectoryData *traj = route.find(n.state.trajectory);

        PathSegment seg;
        seg.trajectory = n.state.trajectory;
        seg.dir = n.state.dir;
        seg.from = n.state.entry;

        if (i + 1 == chain.size())
        {
            seg.to = coord1;
        }
        else
        {
            seg.to = (n.state.dir > 0) ? traj->length : 0.0;
        }

        if (seg.length() > 1e-6 || res.segments.isEmpty())
        {
            res.segments.push_back(seg);
        }

        if (!n.via.isEmpty())
        {
            res.switchNames << n.via;
            ++res.switchesCrossed;
        }

        const int o = route.orientation(seg.trajectory);

        if (o != 0 && o != seg.dir)
        {
            res.wrongWay = true;
        }
    }

    for (const PathSegment &s : std::as_const(res.segments))
    {
        res.length += s.length();
    }

    res.found = true;

    return res;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
GameRoute gameRoute(const Route &route, const QString &start, const QString &target, int dir)
{
    GameRoute res;

    if (route.find(start) == nullptr || route.find(target) == nullptr)
    {
        return res;
    }

    const int d0 = dir < 0 ? -1 : 1;

    if (start == target)
    {
        GameRouteStep st;
        st.trajectory = start;
        st.dir = d0;
        res.steps.push_back(st);
        res.found = true;
        return res;
    }

    struct Prev
    {
        QString key;
        QString sw;
        bool plus = true;
    };

    auto key = [](const QString &t, int d)
    {
        return t + (d > 0 ? QStringLiteral("|+") : QStringLiteral("|-"));
    };

    QHash<QString, Prev> visited;
    std::queue<std::pair<QString, int>> queue;

    visited.insert(key(start, d0), Prev());
    queue.push({ start, d0 });

    QString goal;

    while (!queue.empty() && goal.isEmpty())
    {
        const auto [traj, d] = queue.front();
        queue.pop();

        for (const Route::Endpoint &e : route.crossings(traj, d > 0))
        {
            if (e.trajectory == traj)
            {
                continue;
            }

            const int nd = e.atEnd ? -1 : 1;
            const QString k = key(e.trajectory, nd);

            if (visited.contains(k))
            {
                continue;
            }

            Prev p;
            p.key = key(traj, d);
            p.sw = e.switchName;
            p.plus = e.plus;
            visited.insert(k, p);

            if (e.trajectory == target)
            {
                goal = k;
                break;
            }

            queue.push({ e.trajectory, nd });
        }
    }

    if (goal.isEmpty())
    {
        return res;
    }

    for (QString k = goal; !k.isEmpty(); k = visited.value(k).key)
    {
        GameRouteStep st;
        st.trajectory = k.left(k.size() - 2);
        st.dir = k.endsWith(QLatin1String("|+")) ? 1 : -1;
        st.viaSwitch = visited.value(k).sw;
        st.viaPlus = visited.value(k).plus;
        res.steps.push_back(st);
    }

    std::reverse(res.steps.begin(), res.steps.end());
    res.found = true;

    return res;
}

} // namespace rse
