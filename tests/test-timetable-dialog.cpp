#include    <QtTest>

#include    <QDir>
#include    <QFile>
#include    <QTemporaryDir>

#include    <QComboBox>
#include    <QPushButton>
#include    <QDoubleSpinBox>
#include    <QTableWidget>
#include    <QTimeEdit>

#include    <QLineEdit>
#include    <QListWidget>
#include    <QTimer>
#include    <QToolButton>

#include    "ui/event-dialog.h"
#include    "ui/timetable-dialog.h"
#include    "test-utils.h"

using namespace rse;

namespace
{

//------------------------------------------------------------------------------
// Прямая T1 длиной 8 км вдоль Y (пикетаж = y + 1000), ограничение 60 км/ч на
// 3000...4000 м (пикет 4000...5000); станции A (0), B (4000), C (8000) и боковая D
// на пути Z
//------------------------------------------------------------------------------
void buildRoute(const QString &root)
{
    QString traj;

    for (int y = 0; y <= 8000; y += 500)
    {
        traj += QStringLiteral("0 %1 0 %2\n").arg(y).arg(y + 1000);
    }

    writeFile(root + "/topology/trajectories/T1.traj", traj);
    writeFile(root + "/topology/trajectories/Z.traj", "0 0 0 100\n0 -300 0 -200\n");
    // Изолированная траектория в стороне: ни с чем не соединена - до неё
    // физически нельзя доехать (тест конфликта пути)
    writeFile(root + "/topology/trajectories/Q.traj", "9000 9000 9500 9500\n");
    writeFile(root + "/topology/topology.xml", "<?xml version=\"1.0\"?>\n<Config/>\n");
    writeFile(root + "/topology/stations.conf",
              "Станция-A\t0\t0\t0\n"
              "Станция-B\t0\t4000\t0\n"
              "Станция-C\t0\t8000\t0\n");
    writeFile(root + "/topology/trajectory-speedmap/60.xml",
              "<?xml version=\"1.0\"?>\n<Config>\n"
              "<Trajectory><Name>T1</Name></Trajectory>\n"
              "<SpeedMap><SpeedLimit>60 4000 5000</SpeedLimit></SpeedMap>\n"
              "</Config>\n");
}

PlacedTrain makeTrain()
{
    PlacedTrain t;
    t.name = "P1";
    t.title = "P1";
    t.trajectory = "T1";
    t.coord = 0.0;
    t.dir = 1;
    t.length = 200.0;
    t.mass = 1.0e6;

    return t;
}

} // namespace

class TestTimetableDialog : public QObject
{
    Q_OBJECT

private:

    QTemporaryDir dir_;
    Route route_;
    VehicleCatalog catalog_;

private slots:

    void initTestCase()
    {
        QVERIFY(dir_.isValid());
        buildRoute(dir_.path());
        QVERIFY(route_.load(dir_.path()));
    }

    void dialogFlow()
    {
        Scenario sc;
        sc.add(makeTrain());

        TimetableDialog dlg(route_, catalog_, sc, 0, TimetableState(), nullptr);

        // Без конечной точки применить нельзя
        QVERIFY(!dlg.applyButton()->isEnabled());
        QVERIFY(!dlg.warningsText().isEmpty());

        dlg.setEnd("T1", 8000.0);
        QVERIFY2(dlg.plan().ok, qPrintable(dlg.plan().error));
        QVERIFY(dlg.applyButton()->isEnabled());

        // начало, станция B, конец
        QCOMPARE(dlg.table()->rowCount(), 3);
        QVERIFY(dlg.totalExtraSpin()->value() >= 2.0);

        // Станция B выключена по умолчанию
        QCOMPARE(dlg.timetable().stations.size(), 2);

        dlg.table()->item(1, TimetableDialog::ColUse)->setCheckState(Qt::Checked);
        QCOMPARE(dlg.timetable().stations.size(), 3);
        QVERIFY(!dlg.table()->item(1, TimetableDialog::ColArrival)->text().isEmpty());

        auto *mode = qobject_cast<QComboBox *>(dlg.table()->cellWidget(1, TimetableDialog::ColMode));
        mode->setCurrentIndex(1);
        QVERIFY(dlg.timetable().stations[1].stops());

        const int before = dlg.timetable().stations.last().arrival;
        dlg.totalExtraSpin()->setValue(dlg.totalExtraSpin()->value() + 5.0);
        QVERIFY(dlg.timetable().stations.last().arrival >= before + 4 * 60);

        const int start = dlg.timetable().startTime;
        dlg.startEdit()->setTime(QTime(8, 30));
        QCOMPARE(dlg.timetable().startTime, 8 * 3600 + 30 * 60);
        QVERIFY(dlg.timetable().startTime != start);

        // Состояние переживает повторное открытие
        const Timetable tt = dlg.timetable();
        TimetableDialog again(route_, catalog_, sc, 0, TimetableState::fromTimetable(tt), nullptr);
        QCOMPARE(again.timetable().stations.size(), 3);
        QCOMPARE(again.timetable().stations.last().arrival, tt.stations.last().arrival);
        QCOMPARE(again.timetable().stations[1].departure, tt.stations[1].departure);
    }

    //------------------------------------------------------------------------------
    // Ручные времена: правка ячейки времени, якорь, возврат к авторасчёту
    //------------------------------------------------------------------------------
    void manualTimesEditing()
    {
        Scenario sc;
        sc.add(makeTrain());

        TimetableDialog dlg(route_, catalog_, sc, 0, TimetableState(), nullptr);
        dlg.setEnd("T1", 8000.0);
        QVERIFY2(dlg.plan().ok, qPrintable(dlg.plan().error));

        // Станция B: остановка, ручное прибытие 09:30
        dlg.table()->item(1, TimetableDialog::ColUse)->setCheckState(Qt::Checked);
        auto *mode = qobject_cast<QComboBox *>(dlg.table()->cellWidget(1, TimetableDialog::ColMode));
        mode->setCurrentIndex(1);

        const int manual = 9 * 3600 + 30 * 60;
        dlg.table()->item(1, TimetableDialog::ColArrival)->setText(QStringLiteral("9:30"));

        QCOMPARE(dlg.timetable().stations[1].arrival, manual);
        QVERIFY(dlg.timetable().stations[1].manualArrival);
        QVERIFY(!dlg.timetable().stations[1].manualDeparture);

        // Отправление - авторасчёт от ручного якоря (стоянка 1 мин)
        QCOMPARE(dlg.timetable().stations[1].departure, manual + 60);

        // Конечная станция пересчитана от якоря
        QVERIFY(dlg.timetable().stations[2].arrival >= manual + 120);
        QVERIFY(!dlg.timetable().stations[2].manualArrival);

        // Ручные времена не двигаются при смене времени отправления
        dlg.startEdit()->setTime(QTime(6, 0));
        QCOMPARE(dlg.timetable().stations[1].arrival, manual);

        // Возврат к авторасчёту одной кнопкой
        dlg.recalcAllButton()->click();
        QVERIFY(!dlg.timetable().stations[1].manualArrival);
        QVERIFY(dlg.timetable().stations[1].arrival != manual);

        // Ручное прибытие на конечной станции
        dlg.table()->item(2, TimetableDialog::ColArrival)->setText(QStringLiteral("18:10"));
        QCOMPARE(dlg.timetable().stations[2].arrival, 18 * 3600 + 10 * 60);
        QVERIFY(dlg.timetable().stations[2].manualArrival);

        // Противоречие: конец раньше предыдущего отправления - предупреждение
        dlg.table()->item(2, TimetableDialog::ColArrival)->setText(QStringLiteral("5:00"));
        QCOMPARE(dlg.timetable().stations[2].arrival, 5 * 3600);
        QVERIFY(dlg.warningsText().contains(QStringLiteral("раньше")));

        // Нераспознанный ввод откатывается к прежнему значению
        dlg.table()->item(2, TimetableDialog::ColArrival)->setText(QStringLiteral("когда-нибудь"));
        QCOMPARE(dlg.timetable().stations[2].arrival, 5 * 3600);
    }

    //------------------------------------------------------------------------------
    // Закреплённые станции трассы («через станцию»)
    //------------------------------------------------------------------------------
    void routePins()
    {
        Scenario sc;
        sc.add(makeTrain());

        TimetableDialog dlg(route_, catalog_, sc, 0, TimetableState(), nullptr);
        dlg.setEnd("T1", 8000.0);
        QVERIFY2(dlg.plan().ok, qPrintable(dlg.plan().error));
        QVERIFY(dlg.pinList()->isHidden());

        // Закрепление станции из списка
        const int b_index = dlg.pinCombo()->findText(QStringLiteral("Станция-B"));
        QVERIFY(b_index >= 0);
        dlg.pinCombo()->setCurrentIndex(b_index);
        QCOMPARE(dlg.pinButton()->text(), QStringLiteral("Закрепить"));
        dlg.pinButton()->click();

        QVERIFY2(dlg.plan().ok, qPrintable(dlg.plan().error));
        QCOMPARE(dlg.state().pins.size(), 1);
        QCOMPARE(dlg.state().pins[0].station, QStringLiteral("Станция-B"));
        QCOMPARE(dlg.pinList()->count(), 1);
        QVERIFY(!dlg.pinList()->isHidden());
        QCOMPARE(dlg.pinButton()->text(), QStringLiteral("Убрать"));

        // Закрепление переживает смену конечной точки
        dlg.setEnd("T1", 4000.0);
        QVERIFY2(dlg.plan().ok, qPrintable(dlg.plan().error));
        QCOMPARE(dlg.state().pins.size(), 1);

        // Повторная кнопка убирает закрепление
        dlg.pinButton()->click();
        QCOMPARE(dlg.state().pins.size(), 0);
        QVERIFY(dlg.pinList()->isHidden());
    }

    //------------------------------------------------------------------------------
    // Режимы таблицы «Диспетчер/Тонкая настройка», статус «не применены»,
    // категория поезда (этап 23, аудит 3.4-3.5, 3.8)
    //------------------------------------------------------------------------------
    void dispatcherModeAndDirty()
    {
        Scenario sc;
        sc.add(makeTrain());

        TimetableDialog dlg(route_, catalog_, sc, 0, TimetableState(), nullptr);

        // Открытие диалога - не правка пользователя
        QVERIFY(!dlg.isDirty());
        QVERIFY(!dlg.simpleMode());

        dlg.setEnd("T1", 8000.0);
        QVERIFY2(dlg.plan().ok, qPrintable(dlg.plan().error));

        // Режим «Диспетчер»: служебные столбцы скрыты, времена видны
        dlg.setSimpleMode(true);
        QVERIFY(dlg.simpleMode());
        QVERIFY(dlg.table()->isColumnHidden(TimetableDialog::ColMode));
        QVERIFY(dlg.table()->isColumnHidden(TimetableDialog::ColDwell));
        QVERIFY(dlg.table()->isColumnHidden(TimetableDialog::ColExtra));
        QVERIFY(dlg.table()->isColumnHidden(TimetableDialog::ColTrack));
        QVERIFY(!dlg.table()->isColumnHidden(TimetableDialog::ColArrival));
        QVERIFY(!dlg.table()->isColumnHidden(TimetableDialog::ColDeparture));

        dlg.setSimpleMode(false);
        QVERIFY(!dlg.table()->isColumnHidden(TimetableDialog::ColMode));

        // Правка станционного поля помечает «изменения не применены»
        dlg.table()->item(1, TimetableDialog::ColUse)->setCheckState(Qt::Checked);
        QVERIFY(dlg.isDirty());

        // Категория поезда из положения поезда
        QCOMPARE(dlg.category(), 0);
    }

    //------------------------------------------------------------------------------
    // Платформы станций (этап 27): выбор в таблице попадает в расписание
    // и восстанавливается при повторном открытии
    //------------------------------------------------------------------------------
    void platformsColumn()
    {
        Scenario sc;
        sc.add(makeTrain());

        TimetableDialog dlg(route_, catalog_, sc, 0, TimetableState(), nullptr);
        dlg.setEnd("T1", 8000.0);
        QVERIFY2(dlg.plan().ok, qPrintable(dlg.plan().error));

        // Станция B - строка 1: по умолчанию промежуточные станции выключены
        // и в режиме «Проезд». Платформы задают у станций остановки -
        // включаем станцию и делаем остановку
        dlg.table()->item(1, TimetableDialog::ColUse)->setCheckState(Qt::Checked);

        auto *mode_b = qobject_cast<QComboBox *>(dlg.table()->cellWidget(1, TimetableDialog::ColMode));
        QVERIFY(mode_b != nullptr);
        mode_b->setCurrentIndex(1);
        QVERIFY2(dlg.plan().ok, qPrintable(dlg.plan().error));
        QCOMPARE(dlg.timetable().stations.size(), 3);
        QCOMPARE(dlg.timetable().stations[1].name, QStringLiteral("Станция-B"));

        auto *combo = qobject_cast<QComboBox *>(dlg.table()->cellWidget(1, TimetableDialog::ColPlatform));
        QVERIFY(combo != nullptr);
        QCOMPARE(combo->currentIndex(), 0);
        QCOMPARE(dlg.timetable().stations[1].rightPlatform, -1);

        // «справа и слева» попадает в расписание
        combo->setCurrentIndex(3);
        QVERIFY(dlg.isDirty());
        QCOMPARE(dlg.timetable().stations[1].rightPlatform, 1);
        QCOMPARE(dlg.timetable().stations[1].leftPlatform, 1);
        QCOMPARE(dlg.timetable().stations[2].rightPlatform, -1);

        // Сброс возвращает «не задано»
        combo->setCurrentIndex(0);
        QCOMPARE(dlg.timetable().stations[1].rightPlatform, -1);

        // Повторное открытие графика восстанавливает выбор из расписания
        combo->setCurrentIndex(1);
        const TimetableState st = TimetableState::fromTimetable(dlg.timetable());
        QCOMPARE(st.stations.value(QStringLiteral("Станция-B")).rightPlatform, 1);
        QCOMPARE(st.stations.value(QStringLiteral("Станция-B")).leftPlatform, 0);
    }

    void eventDialogFlow()
    {
        Scenario sc;
        sc.add(makeTrain());

        EventDialog dlg(route_, sc, ScenarioEvent(), 12 * 3600, -1, nullptr);

        // Пока нет действий - нельзя
        QVERIFY(!dlg.okButton()->isEnabled());
        QVERIFY(dlg.problemsText().contains("нет действий"));

        dlg.addButton()->click();
        QCOMPARE(dlg.actionList()->count(), 1);
        QVERIFY(!dlg.okButton()->isEnabled());   // пустые поля

        dlg.fieldA()->setEditText("T1");
        dlg.fieldB()->setEditText("Z");
        QVERIFY(dlg.okButton()->isEnabled());
        QCOMPARE(dlg.event().actions[0].a, QString("T1"));
        QCOMPARE(dlg.event().actions[0].b, QString("Z"));
        QVERIFY(dlg.actionList()->item(0)->text().contains("T1"));

        // Смена типа на стрелку очищает поля траекторий
        dlg.actionTypeCombo()->setCurrentIndex(dlg.actionTypeCombo()->findData(
            static_cast<int>(ActionType::SwitchFwd)));
        emit dlg.actionTypeCombo()->activated(dlg.actionTypeCombo()->currentIndex());
        QCOMPARE(dlg.event().actions[0].type, ActionType::SwitchFwd);
        QVERIFY(dlg.event().actions[0].a.isEmpty());
        QVERIFY(!dlg.okButton()->isEnabled());

        // Триггер "занята траектория"
        dlg.triggerCombo()->setCurrentIndex(dlg.triggerCombo()->findData(
            static_cast<int>(TriggerType::TrajBusy)));
        QCOMPARE(dlg.event().trigger, TriggerType::TrajBusy);

        // Автопропуск: действий не требуется
        dlg.triggerCombo()->setCurrentIndex(dlg.triggerCombo()->findData(
            static_cast<int>(TriggerType::AutoApproach)));
        QCOMPARE(dlg.event().trigger, TriggerType::AutoApproach);
    }

    void routeColumnAndMarks()
    {
        Scenario sc;
        sc.add(makeTrain());

        TimetableDialog dlg(route_, catalog_, sc, 0, TimetableState(), nullptr);

        // Столбец «Маршрут» между отправлением и скоростью
        QCOMPARE(dlg.table()->columnCount(), TimetableDialog::ColCount);
        QCOMPARE(dlg.table()->horizontalHeaderItem(TimetableDialog::ColRoute)->text(),
                 QStringLiteral("Маршрут"));

        int marks_calls = 0;
        int marks_size = -1;
        connect(&dlg, &TimetableDialog::signalMarksChanged,
                [&marks_calls, &marks_size](const QVector<SignalMark> &m)
        {
            ++marks_calls;
            marks_size = m.size();
        });

        // На маршруте без светофоров подсветка пустая, но сигнал с оценкой приходит
        dlg.setEnd("T1", 8000.0);
        QVERIFY2(dlg.plan().ok, qPrintable(dlg.plan().error));
        QVERIFY(marks_calls >= 1);
        QCOMPARE(marks_size, 0);

        // Оценка выполнения графика заполнена у начала и конца
        QVERIFY(!dlg.table()->item(0, TimetableDialog::ColRoute)->text().isEmpty());
        QVERIFY(!dlg.table()->item(1, TimetableDialog::ColRoute)->text().isEmpty());
    }

    //------------------------------------------------------------------------------
    // Удаление станции красным крестом и добавление плюсом между строками:
    // строка исчезает целиком (не галочкой), станция возвращается через «+»
    //------------------------------------------------------------------------------
    void removeAndInsertStations()
    {
        Scenario sc;
        sc.add(makeTrain());

        TimetableDialog dlg(route_, catalog_, sc, 0, TimetableState(), nullptr);
        dlg.setEnd("T1", 8000.0);
        QVERIFY2(dlg.plan().ok, qPrintable(dlg.plan().error));
        QCOMPARE(dlg.table()->rowCount(), 3);   // начало, B, конец

        // У крайних строк креста нет
        QVERIFY(dlg.table()->cellWidget(0, TimetableDialog::ColRemove) == nullptr);
        QVERIFY(dlg.table()->cellWidget(2, TimetableDialog::ColRemove) == nullptr);

        // Красный крест у станции B: строка исчезает целиком
        auto *rm = qobject_cast<QToolButton *>(dlg.table()->cellWidget(1, TimetableDialog::ColRemove));
        QVERIFY(rm != nullptr);
        rm->click();

        QVERIFY2(dlg.plan().ok, qPrintable(dlg.plan().error));
        QCOMPARE(dlg.table()->rowCount(), 2);   // начало, конец
        QCOMPARE(dlg.timetable().stations.size(), 2);
        QVERIFY(dlg.state().removedStations.contains(QStringLiteral("Станция-B")));

        // Убранная станция не возвращается при пересчётах
        dlg.totalExtraSpin()->setValue(dlg.totalExtraSpin()->value() + 2.0);
        QCOMPARE(dlg.table()->rowCount(), 2);

        // Зелёный «+» между строками: новая строка с выпадающим списком станций
        dlg.insertRowAt(1);
        QCOMPARE(dlg.table()->rowCount(), 3);

        auto *combo = qobject_cast<QComboBox *>(dlg.table()->cellWidget(1, TimetableDialog::ColName));
        QVERIFY(combo != nullptr);
        QVERIFY(dlg.table()->item(1, TimetableDialog::ColName)->text().isEmpty());

        combo->setCurrentIndex(combo->findText(QStringLiteral("Станция-B")));
        emit combo->activated(combo->currentIndex());

        // Станция снова в таблице; крест ранее её убрал - «+» возвращает её,
        // и строка включена в график (галочка новой строки стоит по умолчанию)
        QVERIFY2(dlg.plan().ok, qPrintable(dlg.plan().error));
        QCOMPARE(dlg.table()->rowCount(), 3);
        QCOMPARE(dlg.table()->item(1, TimetableDialog::ColName)->text(), QStringLiteral("Станция-B"));
        QVERIFY(dlg.state().removedStations.isEmpty());
        QCOMPARE(dlg.timetable().stations.size(), 3);

        // Станция стала обычной строкой плана, а не добавленной
        QVERIFY(dlg.state().addedStations.isEmpty());

        // Состояние переживает повторное открытие
        const Timetable tt = dlg.timetable();
        TimetableDialog again(route_, catalog_, sc, 0, TimetableState::fromTimetable(tt), nullptr);
        QCOMPARE(again.timetable().stations.size(), 3);
    }

    //------------------------------------------------------------------------------
    // Конфликт пути: график не сбрасывается, станция-виновник подсвечивается
    // лёгким красным, предупреждение с её именем
    //------------------------------------------------------------------------------
    void planFailureKeepsTimetable()
    {
        Scenario sc;
        sc.add(makeTrain());

        TimetableDialog dlg(route_, catalog_, sc, 0, TimetableState(), nullptr);
        dlg.setEnd("T1", 8000.0);
        QVERIFY2(dlg.plan().ok, qPrintable(dlg.plan().error));
        QCOMPARE(dlg.table()->rowCount(), 3);

        dlg.table()->item(1, TimetableDialog::ColUse)->setCheckState(Qt::Checked);

        const int rows = dlg.table()->rowCount();
        const int arrival_end = dlg.timetable().stations.last().arrival;
        const QString summary = dlg.summaryText();

        // Выбираем станции путь, до которого доехать физически нельзя
        // (изолированная траектория Q ни с чем не соединена)
        auto *track = qobject_cast<QComboBox *>(dlg.table()->cellWidget(1, TimetableDialog::ColTrack));
        QVERIFY(track != nullptr);
        track->addItem(QStringLiteral("Q"), QStringLiteral("Q"));
        track->setCurrentIndex(track->count() - 1);
        emit track->activated(track->currentIndex());

        // График остался прежним: таблица, времена и сводка на месте
        QVERIFY2(dlg.plan().ok, qPrintable(dlg.plan().error));
        QCOMPARE(dlg.table()->rowCount(), rows);
        QCOMPARE(dlg.timetable().stations.last().arrival, arrival_end);
        QCOMPARE(dlg.summaryText(), summary);

        // Предупреждение называет станцию-виновника
        QVERIFY(dlg.warningsText().contains(QStringLiteral("Станция-B")));

        // Строка-виновник подсвечена лёгким красным
        QCOMPARE(dlg.table()->item(1, TimetableDialog::ColName)->background().color(),
                 QColor(255, 214, 214));

        // Вернули «авто» - график снова пересчитан, подсветка снята
        track->setCurrentIndex(0);
        emit track->activated(0);
        QVERIFY2(dlg.plan().ok, qPrintable(dlg.plan().error));
        QCOMPARE(dlg.table()->item(1, TimetableDialog::ColName)->background(), QBrush());
    }

    //------------------------------------------------------------------------------
    // Подчёркивание всей ячейки времени (делегат у колонок прибытия/отправления)
    // и изменяемое окно графика
    //------------------------------------------------------------------------------
    void underlineDelegateAndWindow()
    {
        Scenario sc;
        sc.add(makeTrain());

        TimetableDialog dlg(route_, catalog_, sc, 0, TimetableState(), nullptr);

        // У колонок времени установлен делегат: линия во всю ширину ячейки
        QVERIFY(dlg.table()->itemDelegateForColumn(TimetableDialog::ColArrival) != nullptr);
        QVERIFY(dlg.table()->itemDelegateForColumn(TimetableDialog::ColDeparture) != nullptr);

        // Окно изменяемого размера: есть минимальный размер, стартовый больше него
        QVERIFY(dlg.minimumSize().width() >= 400);
        QVERIFY(dlg.size().width() >= dlg.minimumSize().width());

        // Подчёркивание больше не делается шрифтом
        dlg.setEnd("T1", 8000.0);
        QVERIFY2(dlg.plan().ok, qPrintable(dlg.plan().error));
        QVERIFY(!dlg.table()->item(1, TimetableDialog::ColArrival)->font().underline());
        QVERIFY(!dlg.table()->item(1, TimetableDialog::ColDeparture)->font().underline());
    }
};

QTEST_MAIN(TestTimetableDialog)
#include "test-timetable-dialog.moc"
