#ifndef     RSE_SCENARIO_H
#define     RSE_SCENARIO_H

#include    "catalog/catalog-data.h"
#include    "scenario/footprint.h"

#include    <QString>
#include    <QStringList>
#include    <QVector>

namespace rse
{

//------------------------------------------------------------------------------
// Станция расписания поезда (файл timetable/<поезд>.xml)
//------------------------------------------------------------------------------
struct TimetableStation
{
    QString name;

    /// Время в секундах от начала суток. Прибытие и отправление равны - проезд без остановки
    int arrival = 0;
    int departure = 0;

    /// Куда должна встать голова поезда на станции
    QString targetTrajectory;
    double targetCoord = 0.0;

    /// Поля расписания, прочитанные из существующего файла и сохраняемые как есть
    /// (пусто / -1 - не заданы; редактор сам строит подход и уход по пути)
    QString approachTrajectory;
    QString removalTrajectory;
    int rightPlatform = -1;
    int leftPlatform = -1;
    int visible = -1;

    /// Личный запас на перегон до станции, мин (для повторного открытия графика)
    double extraMin = 0.0;

    /// Ручные времена строки: заданы пользователем в окне графика и идут
    /// в экспорт как введены (переживают пересчёт и сохраняются в проекте)
    bool manualArrival = false;
    bool manualDeparture = false;

    bool stops() const
    {
        return departure > arrival;
    }
};

//------------------------------------------------------------------------------
// Точка движения поезда по расписанию (для проверки пересечений с другими поездами)
//------------------------------------------------------------------------------
struct TimetableSample
{
    int time = 0;
    QString trajectory;
    double coord = 0.0;
    int dir = 1;
};

//------------------------------------------------------------------------------
// Расписание поезда, рассчитанное редактором
//------------------------------------------------------------------------------
struct Timetable
{
    bool valid = false;

    /// Время отправления, с от начала суток
    int startTime = 0;

    /// Конечная точка пути (голова поезда)
    QString endTrajectory;
    double endCoord = 0.0;

    /// Запас на весь путь, мин
    double totalExtraMin = 0.0;

    QVector<TimetableStation> stations;

    /// Движение головы поезда, для проверки пересечений
    QVector<TimetableSample> samples;
};

//------------------------------------------------------------------------------
// Закреплённая промежуточная станция трассы графика («через станцию»)
//------------------------------------------------------------------------------
struct TripPin
{
    /// Имя станции из маршрута
    QString station;

    /// Выбранный путь станции (пусто - ближайший к станции путь)
    QString track;

    /// Координата на траектории пути (для track)
    double trackCoord = 0.0;
};

//------------------------------------------------------------------------------
// Состав, поставленный на путь
//------------------------------------------------------------------------------
struct PlacedTrain
{
    /// Имя поезда в сценарии (уникальное)
    QString name;

    /// Имя переменной Lua в main.lua (у импортированных поездов - исходное, чтобы
    /// сохранённый код на неё ссылался); пусто - editor выберет сам
    QString luaVar;

    /// Название состава для списка (например, имя готового состава)
    QString title;

    /// Исходный состав из каталога (имя файла cfg/trains), если есть
    QString sourceTrain;

    QVector<ConsistGroup> groups;

    /// Длина, м
    double length = 0.0;

    /// Масса, кг
    double mass = 0.0;

    /// Автоведение возможно (у крайней единицы есть автопилот)
    bool autopilotCapable = false;

    /// Позиция головы
    QString trajectory;
    double coord = 0.0;
    int dir = 1;

    /// Бот: автозапуск и автоведение (флаг auto в сценарии)
    bool autoMode = false;

    /// Категория поезда для очерёдности пропуска (TrainClass; 0 - по номеру)
    int trainClass = 0;

    /// Не создавать события маршрутов по графику этого поезда
    bool noAutoEvents = false;

    /// Закреплённые промежуточные станции трассы графика: путь строится
    /// по порядку через них (переживают пересчёт, хранятся в проекте)
    QVector<TripPin> pins;

    /// Расписание (сбрасывается, если меняется положение или состав)
    Timetable timetable;

    /// Продолжение: поезд, который появится после того, как локомотив formedFrom
    /// прибудет по своему графику к стоящим вагонам formedWagons и сцепится с ними.
    /// Такой поезд на карте не стоит, setTrain для него не пишется - в сценарий попадает
    /// только его график и переименование поезда в момент сцепки
    QString formedFrom;
    QString formedWagons;

    /// Через сколько секунд после прибытия локомотива переименовать поезд (сцепка и проба тормозов)
    int formDelaySec = 120;

    bool isContinuation() const
    {
        return !formedFrom.isEmpty();
    }

    /// Будущий поезд: на карте его в начале сценария нет, в сценарий попадает только
    /// его график (по имени поезда). Нужен, когда поезд появится позже (переименование
    /// после сцепки, формирование) и график заранее подготавливается к его номеру
    bool planned = false;

    /// В сценарий не выставляется
    bool isVirtual() const
    {
        return planned || isContinuation();
    }
};

//------------------------------------------------------------------------------
// Результат проверки положения состава
//------------------------------------------------------------------------------
struct PlacementCheck
{
    Footprint footprint;

    /// Пересечение с другими составами, м (ошибка: поставить нельзя)
    double overlap = 0.0;
    QStringList overlapWith;

    /// Другие составы в пределах зоны автосцепки (предупреждение)
    QStringList nearTrains;

    bool hasProblems() const
    {
        return overlap > 0.0 || footprint.deadEnd;
    }
};

//------------------------------------------------------------------------------
// Расстановка: список составов на путях
//------------------------------------------------------------------------------
class Scenario
{
public:

    /// Дальность автосцепки в симуляторе, м (DISTANCE_TO_COUPLE_TRAINS)
    static constexpr double COUPLE_DISTANCE = 10.0;

    const QVector<PlacedTrain> &trains() const
    {
        return trains_;
    }

    int size() const
    {
        return trains_.size();
    }

    bool isEmpty() const
    {
        return trains_.isEmpty();
    }

    /// Добавить состав; возвращает его индекс
    int add(const PlacedTrain &train);

    /// Заменить состав по индексу
    void replace(int index, const PlacedTrain &train);

    void remove(int index);

    void clear();

    int indexOfName(const QString &name) const;

    /// Свободное имя на основе base ("Поезд 1", "Поезд 2", …)
    QString uniqueName(const QString &base, int ignore_index = -1) const;

    /// Проверить положение train (возможно ещё не добавленного); состав с
    /// индексом ignore_index при проверке пропускается
    PlacementCheck check(const Route &route, const PlacedTrain &train,
                         int ignore_index = -1) const;

    /// Состав, повёрнутый на месте: голова там, где был хвост, смотрит
    /// в противоположную сторону; порядок и ориентация групп вагонов обращены
    static PlacedTrain reversedInPlace(const Route &route, const PlacedTrain &train);

    /// Состав с головой, развёрнутой в противоположную сторону без сдвига
    /// (при расстановке: голова остаётся в точке клика)
    static PlacedTrain withFlippedHead(const PlacedTrain &train);

private:

    QVector<PlacedTrain> trains_;
};

} // namespace rse

#endif // RSE_SCENARIO_H
