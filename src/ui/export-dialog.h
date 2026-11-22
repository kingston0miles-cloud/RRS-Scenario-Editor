#ifndef     RSE_EXPORT_DIALOG_H
#define     RSE_EXPORT_DIALOG_H

#include    <QDialog>

#include    "scenario/export.h"

class QCheckBox;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QTimeEdit;

namespace rse
{

//------------------------------------------------------------------------------
// Экспорт сценария: имя, время начала, описание; сразу показывает, что будет
// записано, и что мешает записи
//------------------------------------------------------------------------------
class ExportDialog : public QDialog
{
    Q_OBJECT

public:

    ExportDialog(const Route &route, const VehicleCatalog &catalog, const Scenario &scenario,
                 const ProjectInfo &info, QWidget *parent = nullptr);

    /// Параметры, выбранные пользователем
    ExportOptions options() const;

    /// Для проверок
    QLineEdit *nameEdit() const
    {
        return name_edit_;
    }

    QCheckBox *overwriteCheck() const
    {
        return overwrite_;
    }

    QPushButton *exportButton() const
    {
        return export_button_;
    }

    QString previewText() const;

private slots:

    void update();

private:

    const Route &route_;
    const VehicleCatalog &catalog_;
    const Scenario &scenario_;

    QLineEdit *name_edit_ = nullptr;
    QCheckBox *auto_start_ = nullptr;
    QTimeEdit *start_edit_ = nullptr;
    QPlainTextEdit *description_ = nullptr;
    QCheckBox *overwrite_ = nullptr;
    QPlainTextEdit *preview_ = nullptr;
    QPushButton *export_button_ = nullptr;

    ProjectInfo info_;
};

} // namespace rse

#endif // RSE_EXPORT_DIALOG_H
