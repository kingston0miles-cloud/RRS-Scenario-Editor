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

#include    "ui/event-dialog.h"
#include    "ui/timetable-dialog.h"

using namespace rse;

namespace
{

void writeFile(const QString &path, const QString &content)
{
    QDir().mkpath(QFileInfo(path).absolutePath());

    QFile f(path);
    QVERIFY2(f.open(QIODevice::WriteOnly), qPrintable(path));
    f.write(content.toUtf8());
}

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
};

QTEST_MAIN(TestTimetableDialog)
#include "test-timetable-dialog.moc"
