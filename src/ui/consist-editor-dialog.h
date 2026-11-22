#ifndef     RSE_CONSIST_EDITOR_DIALOG_H
#define     RSE_CONSIST_EDITOR_DIALOG_H

#include    <QDialog>

#include    "scenario/consist.h"

class QCheckBox;
class QDialogButtonBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QVBoxLayout;
class QPushButton;
class QTreeWidget;

namespace rse
{

//------------------------------------------------------------------------------
// Правка состава поезда: добавить любые единицы и готовые составы, убрать,
// переставить, задать загрузку и ориентацию, разорвать состав на несколько
// поездов (части ставятся друг за другом с зазором)
//------------------------------------------------------------------------------
class ConsistEditorDialog : public QDialog
{
    Q_OBJECT

public:

    /// index - индекс правимого поезда в сценарии (для проверки пересечений)
    ConsistEditorDialog(const Scenario &scenario, const Route &route,
                        const VehicleCatalog &catalog, const PlacedTrain &train,
                        int index, QWidget *parent = nullptr);

    /// Режим создания состава для cfg/trains: без карты и поездов, с названием,
    /// описанием и давлениями. initial - заготовка (пустые groups - новый состав)
    ConsistEditorDialog(const VehicleCatalog &catalog, const TrainConfigData &initial,
                        QWidget *parent = nullptr);

    /// Состав как конфиг cfg/trains (режим создания)
    TrainConfigData trainConfig() const;

    /// Поле «Имя файла» режима создания (для проверок)
    QLineEdit *fileNameEdit() const
    {
        return file_edit_;
    }

    /// Состав после правки и места разрыва - то, что принимает planEdit()
    PlacedTrain edited() const;
    QVector<int> cuts() const;

    /// Итоговый план (поезда и ошибки)
    EditPlan plan() const;

    //--------------------------------------------------------------------------
    // Действия программно (их выполняют и кнопки); нужны тестам
    //--------------------------------------------------------------------------

    int unitCount() const
    {
        return units_.size();
    }

    /// Добавить единицу из каталога; position < 0 - в конец
    bool addVehicle(const QString &config_dir, const QString &config, int count,
                    double payload, int position = -1);

    /// Добавить все единицы готового состава; position < 0 - в конец
    bool addTrain(const QString &name, int count, int position = -1);

    void removeUnits(const QVector<int> &rows);
    void moveUnit(int row, int delta);
    void setPayload(const QVector<int> &rows, double coeff);
    void flipUnits(const QVector<int> &rows);
    void setCutBefore(int row, bool cut);

    QPushButton *okButton() const;
    QString messagesText() const;

private:

    struct Unit
    {
        ConsistGroup group;
        bool cutBefore = false;
    };

    const Scenario &scenario_;
    const Route &route_;
    bool composer_ = false;
    TrainConfigData base_;
    const VehicleCatalog &catalog_;
    PlacedTrain train_;
    int index_ = -1;
    QVector<Unit> units_;

    QTreeWidget *tree_ = nullptr;
    QLabel *summary_label_ = nullptr;
    QLabel *messages_label_ = nullptr;
    QDoubleSpinBox *payload_box_ = nullptr;
    QCheckBox *flip_box_ = nullptr;
    QCheckBox *cut_box_ = nullptr;
    QCheckBox *auto_box_ = nullptr;
    QVBoxLayout *main_layout_ = nullptr;
    QLabel *hint_label_ = nullptr;
    QLineEdit *file_edit_ = nullptr;
    QLineEdit *title_edit_ = nullptr;
    QLineEdit *desc_edit_ = nullptr;
    QDoubleSpinBox *charge_box_ = nullptr;
    QDoubleSpinBox *mainres_box_ = nullptr;
    QCheckBox *noair_box_ = nullptr;
    QDialogButtonBox *buttons_ = nullptr;
    bool updating_ = false;

    QVector<int> selectedRows() const;
    void rebuild(const QVector<int> &select = QVector<int>());
    void refreshInfo();
    void onSelectionChanged();
    void onAddClicked();
};

} // namespace rse

#endif // RSE_CONSIST_EDITOR_DIALOG_H
