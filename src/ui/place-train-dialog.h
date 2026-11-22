#ifndef     RSE_PLACE_TRAIN_DIALOG_H
#define     RSE_PLACE_TRAIN_DIALOG_H

#include    <QDialog>

#include    "scenario/scenario.h"

class QCheckBox;
class QComboBox;
class QDialogButtonBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;

namespace rse
{

//------------------------------------------------------------------------------
// Подтверждение расстановки: состав, путь, координата, направление головы,
// имя и флаг бота. Предупреждения обновляются при каждом изменении
//------------------------------------------------------------------------------
class PlaceTrainDialog : public QDialog
{
    Q_OBJECT

public:

    /// train - заготовка с составом (groups, length, …); trajectory, coord и dir
    /// заданы местом клика. scenario - уже стоящие составы; ignore_index - состав,
    /// который редактируется (-1 для нового)
    PlaceTrainDialog(const Route &route, const Scenario &scenario,
                     const PlacedTrain &train, int ignore_index,
                     QWidget *parent = nullptr);

    /// Итог диалога
    PlacedTrain result() const;

    /// Текст направления головы для (траектория, dir): «туда» / «обратно»
    static QString directionText(const Route &route, const QString &trajectory, int dir);

    /// Кнопка «Развернуть» (для проверок)
    QPushButton *flipButton() const
    {
        return flip_button_;
    }

    /// Кнопка «Поставить» (для проверок)
    QPushButton *okButton() const;

    /// Текст предупреждений (для проверок)
    QString warningsText() const;

    QComboBox *trajectoryBox() const
    {
        return traj_box_;
    }

    QComboBox *waypointBox() const
    {
        return waypoint_box_;
    }

    QDoubleSpinBox *coordBox() const
    {
        return coord_box_;
    }

    QLineEdit *nameEdit() const
    {
        return name_edit_;
    }

    QCheckBox *autoBox() const
    {
        return auto_box_;
    }

private slots:

    void onFlip();
    void refresh();
    void onTrajectoryEdited(const QString &text);
    void onWaypointChosen(int index);

private:

    const Route &route_;
    const Scenario &scenario_;
    PlacedTrain train_;
    int ignore_index_ = -1;

    QComboBox *traj_box_ = nullptr;
    QComboBox *waypoint_box_ = nullptr;
    QLabel *direction_label_ = nullptr;
    QLabel *warnings_label_ = nullptr;
    QLineEdit *name_edit_ = nullptr;
    QDoubleSpinBox *coord_box_ = nullptr;
    QPushButton *flip_button_ = nullptr;
    QCheckBox *auto_box_ = nullptr;
    QDialogButtonBox *buttons_ = nullptr;

    PlacedTrain current() const;
};

} // namespace rse

#endif // RSE_PLACE_TRAIN_DIALOG_H
