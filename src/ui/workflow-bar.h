#ifndef     RSE_WORKFLOW_BAR_H
#define     RSE_WORKFLOW_BAR_H

#include    <QWidget>

class QPushButton;

namespace rse
{

//------------------------------------------------------------------------------
// Панель шагов работы над сценарием: «1 · Маршрут → 2 · Составы → 3 · Расстановка
// → 4 · Графики → 5 · Проверка → 6 · Экспорт» (аудит 1.3). Шаг с отмеченным
// выполнением выделяется, текущий (первый невыполненный) — жирный; щелчок
// выполняет соответствующее действие
//------------------------------------------------------------------------------
class WorkflowBar : public QWidget
{
    Q_OBJECT

public:

    /// Что уже готово к работе на каждом шаге
    struct State
    {
        bool route = false;         // маршрут открыт
        bool catalog = false;       // подвижной состав загружен
        bool trains = false;        // есть расстановка
        bool timetables = false;    // есть хотя бы один график
        bool exported = false;      // сценарий экспортирован

        bool done(int step) const;
    };

    explicit WorkflowBar(QWidget *parent = nullptr);

    /// Обновить состояние шагов (done - что выполнено)
    void setState(const State &state);

    /// Внутреннее состояние (для проверок): done, current, titles
    State state() const
    {
        return state_;
    }

    /// Текущий шаг (первый невыполненный), 1..6
    int currentStep() const;

    /// Названия шагов (для проверок)
    QStringList stepTitles() const;

signals:

    /// Пользователь нажал шаг 1..6
    void stepActivated(int step);

private:

    State state_;
    QPushButton *buttons_[6] = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};

    void restyle(int index);
};

} // namespace rse

#endif // RSE_WORKFLOW_BAR_H
