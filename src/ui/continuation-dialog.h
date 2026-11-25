#ifndef     RSE_CONTINUATION_DIALOG_H
#define     RSE_CONTINUATION_DIALOG_H

#include    <QDialog>

#include    "scenario/formation.h"

class QComboBox;
class QDialogButtonBox;
class QLabel;
class QLineEdit;
class QSpinBox;

namespace rse
{

//------------------------------------------------------------------------------
// Продолжение: локомотив приходит к вагонам, сцепляется и дальше едет новый поезд
//------------------------------------------------------------------------------
class ContinuationDialog : public QDialog
{
    Q_OBJECT

public:

    /// loco_index - локомотив; cont - заготовка продолжения (formedFrom заполнен)
    ContinuationDialog(const Route &route, const Scenario &scenario, int loco_index,
                       const PlacedTrain &cont, int ignore_index, QWidget *parent = nullptr);

    /// Итог: продолжение с названием, вагонами и задержкой; положение и состав
    /// рассчитаны по графику локомотива
    PlacedTrain result() const;

    QComboBox *wagonsBox() const
    {
        return wagons_box_;
    }

    QLineEdit *nameEdit() const
    {
        return name_edit_;
    }

    QSpinBox *delaySpin() const
    {
        return delay_spin_;
    }

    QPushButton *okButton() const;

    QString infoText() const;

private:

    const Route &route_;
    const Scenario &scenario_;
    PlacedTrain cont_;
    int ignore_index_ = -1;

    QComboBox *wagons_box_ = nullptr;
    QLineEdit *name_edit_ = nullptr;
    QSpinBox *delay_spin_ = nullptr;
    QLabel *info_ = nullptr;
    QDialogButtonBox *buttons_ = nullptr;

    PlacedTrain current() const;
    void refresh();
};

} // namespace rse

#endif // RSE_CONTINUATION_DIALOG_H
