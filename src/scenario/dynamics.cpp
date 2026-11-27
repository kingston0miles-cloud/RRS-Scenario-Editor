#include    "scenario/dynamics.h"

#include    <QObject>
#include    <QtMath>

#include    <algorithm>
#include    <cmath>

namespace rse
{

namespace
{

constexpr double G = 9.81;

/// Приближённые данные локомотивов: часть имени конфига, мощность, сила тяги
/// при трогании, максимальная скорость
struct LocoGuess
{
    const char *prefix;
    double kw;
    double startKn;
    double vmax;
};

const LocoGuess LOCO_TABLE[] =
{
    {"vl60", 4600.0, 400.0, 100.0},
    {"vl80", 6520.0, 640.0, 110.0},
    {"vl85", 9000.0, 800.0, 110.0},
    {"chs2t", 5100.0, 400.0, 160.0},
    {"chs2", 5100.0, 400.0, 160.0},
    {"chs4", 6000.0, 450.0, 160.0},
    {"chs7", 6160.0, 450.0, 160.0},
    {"chs8", 8400.0, 600.0, 120.0},
    {"ep20", 7200.0, 450.0, 160.0},
    {"2te116", 4500.0, 500.0, 100.0},
    {"2te10", 4500.0, 500.0, 100.0},
    {"tep60", 2950.0, 250.0, 160.0},
    {"tep70", 2950.0, 250.0, 160.0},
    {"2es5", 6560.0, 600.0, 120.0},
    {"3es5", 9840.0, 900.0, 120.0}
};

const LocoGuess *findGuess(const QString &config)
{
    const QString name = config.toLower();

    for (const LocoGuess &g : LOCO_TABLE)
    {
        if (name.startsWith(QLatin1String(g.prefix)))
        {
            return &g;
        }
    }

    return nullptr;
}

/// Коэффициент сцепления колёс с рельсами, как в симуляторе (локомотив
/// переменного тока), V - км/ч
double adhesion(double v_ms)
{
    const double V = v_ms * 3.6;

    return 0.28 + 4.0 / (50.0 + 6.0 * V) - 0.0006 * V;
}

} // namespace

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
double TrainDynamics::resistance(double v) const
{
    return a0 + a1 * v + a2 * v * v;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
double TrainDynamics::tractiveForce(double v) const
{
    const double vmax = traction.vMaxKmh / 3.6;

    if (v >= vmax)
    {
        return 0.0;
    }

    double f = traction.startForceKn * 1000.0;

    if (v > 0.1)
    {
        f = std::min(f, traction.powerKw * 1000.0 / v);
    }

    if (traction.adhesiveMass > 0.0)
    {
        f = std::min(f, 0.9 * adhesion(v) * traction.adhesiveMass * G);
    }

    return f;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
TractionProfile guessTraction(const QVector<ConsistGroup> &groups, const VehicleCatalog &catalog)
{
    TractionProfile tp;
    tp.powerKw = 0.0;
    tp.startForceKn = 0.0;
    tp.vMaxKmh = 0.0;

    int locos = 0;
    int unknown = 0;
    bool first = true;

    for (const ConsistGroup &g : groups)
    {
        const VehicleConfigData *v = catalog.findVehicle(g.configDir, g.config);

        if ((v == nullptr) || !v->isLocomotive)
        {
            continue;
        }

        const LocoGuess *guess = findGuess(v->config);

        for (int i = 0; i < g.count; ++i)
        {
            ++locos;

            double kw = 3000.0;
            double kn = 300.0;
            double vmax = (v->constructionSpeed > 0.0) ? v->constructionSpeed : 100.0;

            if (guess != nullptr)
            {
                kw = guess->kw;
                kn = guess->startKn;
                vmax = (v->constructionSpeed > 0.0) ? v->constructionSpeed : guess->vmax;
            }
            else
            {
                ++unknown;
                tp.notes << QObject::tr("Тяга «%1» неизвестна, принята по умолчанию").arg(v->id());
            }

            tp.powerKw += kw;
            tp.startForceKn += kn;
            tp.adhesiveMass += v->emptyMass;
            tp.vMaxKmh = first ? vmax : std::min(tp.vMaxKmh, vmax);
            first = false;
        }
    }

    if (locos == 0)
    {
        tp.powerKw = 3000.0;
        tp.startForceKn = 300.0;
        tp.vMaxKmh = 100.0;
        tp.notes << QObject::tr("В составе нет локомотивов: принята тяга по умолчанию");
    }
    else if (unknown < locos)
    {
        tp.notes << QObject::tr("Тяга оценена приближённо (%1 лок.); при необходимости поправьте")
                    .arg(locos);
    }

    // Конструкционные скорости вагонов тоже ограничивают состав (этап 26:
    // V_constr читается прямо из конфига единицы)
    for (const ConsistGroup &g : groups)
    {
        const VehicleConfigData *v = catalog.findVehicle(g.configDir, g.config);

        if (v == nullptr || v->isLocomotive || v->constructionSpeed <= 0.0)
        {
            continue;
        }

        if (tp.vMaxKmh <= 0.0 || v->constructionSpeed < tp.vMaxKmh)
        {
            tp.vMaxKmh = v->constructionSpeed;
            tp.notes << QObject::tr("Скорость ограничена вагоном «%1»: %2 км/ч")
                            .arg(v->id())
                            .arg(v->constructionSpeed, 0, 'f', 0);
        }
    }

    // Чугунные тормозные колодки тормозят слабее композитных; точного значения
    // в конфигах нет, поэтому - предупреждение, а не поправка расчёта
    for (const ConsistGroup &g : groups)
    {
        if (g.brakeShoes)
        {
            tp.notes << QObject::tr("В составе есть единицы на чугунных колодках: "
                                    "служебное замедление может быть завышено");
            break;
        }
    }

    return tp;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
TrainDynamics trainDynamics(const PlacedTrain &train, const VehicleCatalog &catalog)
{
    TrainDynamics d;

    d.length = train.length;

    for (const ConsistGroup &g : train.groups)
    {
        const VehicleConfigData *v = catalog.findVehicle(g.configDir, g.config);

        if (v == nullptr)
        {
            continue;
        }

        const double m = v->emptyMass + v->payloadMass * g.payloadCoeff;

        double q = 1.0;

        if (v->resist.q0 > 1.0)
        {
            q = m / (1000.0 * std::max(1, v->numAxis));
        }

        const double b0 = v->resist.b0;
        const double b1 = v->resist.b1;
        const double b2 = v->resist.b2;
        const double b3 = v->resist.b3;

        const double w = (b0 + b1 / q) * G / 1000.0;
        const double wv = (b2 / q) * G * 3.6 / 1000.0;
        const double wv2 = (b3 / q) * G * 3.6 * 3.6 / 1000.0;

        d.a0 += g.count * m * w;
        d.a1 += g.count * m * wv;
        d.a2 += g.count * m * wv2;
        d.mass += g.count * m;
    }

    if (d.mass <= 0.0)
    {
        d.mass = train.mass;
    }

    d.traction = guessTraction(train.groups, catalog);

    return d;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
double RunResult::timeAt(double s, double step) const
{
    if (time.isEmpty())
    {
        return 0.0;
    }

    const double x = std::max(0.0, s / step);
    const int i = std::min(static_cast<int>(x), static_cast<int>(time.size()) - 1);

    if (i + 1 >= time.size())
    {
        return time.last();
    }

    return time[i] + (time[i + 1] - time[i]) * (x - i);
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
RunResult simulateRun(const TrainDynamics &dyn, const TrackProfile &profile,
                      const QVector<int> &stops)
{
    RunResult r;

    const int n = profile.size();

    if (n < 2)
    {
        r.error = QObject::tr("Путь слишком короткий");
        return r;
    }

    if (dyn.mass <= 0.0)
    {
        r.error = QObject::tr("Не удалось определить массу состава");
        return r;
    }

    const double ds = profile.step;
    const double vmax = dyn.traction.vMaxKmh / 3.6;

    // Огибающая допустимой скорости: ограничения, v = 0 на остановках,
    // торможение заранее с замедлением brakeDecel
    QVector<double> cap(n);
    QVector<bool> is_stop(n, false);

    for (int i = 0; i < n; ++i)
    {
        cap[i] = std::min(profile.limit[i], vmax);
    }

    is_stop[0] = true;
    is_stop[n - 1] = true;

    for (int s : stops)
    {
        if ((s >= 0) && (s < n))
        {
            is_stop[s] = true;
        }
    }

    for (int i = 0; i < n; ++i)
    {
        if (is_stop[i])
        {
            cap[i] = 0.0;
        }
    }

    const double b = std::max(0.1, dyn.traction.brakeDecel);

    for (int i = n - 2; i >= 0; --i)
    {
        cap[i] = std::min(cap[i], std::sqrt(cap[i + 1] * cap[i + 1] + 2.0 * b * ds));
    }

    // Разгон вперёд. Уклон усредняется по длине состава
    const int len_nodes = std::max(1, static_cast<int>(dyn.length / ds));

    r.speed.fill(0.0, n);
    r.time.fill(0.0, n);

    bool weak_reported = false;
    constexpr double V_MIN = 0.5;

    for (int i = 0; i < n - 1; ++i)
    {
        double v = r.speed[i];

        if (is_stop[i])
        {
            v = 0.0;
        }

        const int j = std::max(0, i - len_nodes);
        const double dh = profile.height[i] - profile.height[j];
        const double dl = (i - j) * ds;
        const double grade = (dl > 1.0) ? dh / dl : 0.0;

        const double vv = std::max(v, V_MIN);
        const double force = dyn.tractiveForce(vv) - dyn.resistance(vv)
                           - dyn.mass * G * grade;
        const double a = force / dyn.mass;

        double v2 = v * v + 2.0 * a * ds;

        if (is_stop[i])
        {
            // из состояния покоя: v^2 = 2 a ds
            v2 = 2.0 * std::max(a, 0.0) * ds;
        }

        double vn = std::sqrt(std::max(v2, 0.0));

        if (vn < V_MIN && cap[i + 1] > V_MIN)
        {
            if (!weak_reported)
            {
                r.warnings << QObject::tr("Не хватает тяги для движения с уклоном около "
                                          "%1 ‰ (путь %2 м): принята скорость %3 км/ч")
                              .arg(grade * 1000.0, 0, 'f', 1)
                              .arg(i * ds, 0, 'f', 0)
                              .arg(V_MIN * 3.6, 0, 'f', 1);
                weak_reported = true;
            }

            vn = V_MIN;
        }

        r.speed[i + 1] = std::min(vn, cap[i + 1]);
    }

    // Время: на каждом шаге средняя скорость
    for (int i = 0; i < n - 1; ++i)
    {
        const double v0 = r.speed[i];
        const double v1 = r.speed[i + 1];
        double avg = 0.5 * (v0 + v1);

        if (avg < 0.3)
        {
            avg = 0.3;
        }

        r.time[i + 1] = r.time[i] + ds / avg;
    }

    r.ok = true;

    return r;
}

} // namespace rse
