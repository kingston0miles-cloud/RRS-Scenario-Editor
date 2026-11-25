#ifndef     RSE_CHECK_DIALOG_H
#define     RSE_CHECK_DIALOG_H

#include    <QDialog>

#include    <functional>

#include    "scenario/checker.h"

class QCheckBox;
class QLabel;
class QPushButton;
class QTableWidget;

namespace rse
{

//------------------------------------------------------------------------------
// Список проблем в расписаниях сценария. Окно остаётся открытым при работе
// с картой; «Проверить снова» пересчитывает результат
//------------------------------------------------------------------------------
class CheckDialog : public QDialog
{
    Q_OBJECT

public:

    using Provider = std::function<QVector<ScheduleIssue>()>;

    CheckDialog(Provider provider, QWidget *parent = nullptr);

    /// Пересчитать и показать
    void recheck();

    int issueCount() const
    {
        return issues_.size();
    }

    int shownCount() const;

    QTableWidget *table() const
    {
        return table_;
    }

    QString summaryText() const;

    /// Текст отчёта целиком (для копирования)
    QString reportText() const;

signals:

    /// Двойной щелчок по проблеме
    void issueActivated(const QStringList &trains, const QString &trajectory);

    /// «Добавить стоянку для пропуска»: имя поезда, в график которого
    /// предлагается добавить стоянку (аудит 3.7)
    void parkRequested(const QString &train);

private:

    Provider provider_;
    QVector<ScheduleIssue> issues_;

    QTableWidget *table_ = nullptr;
    QLabel *summary_ = nullptr;
    QCheckBox *only_errors_ = nullptr;
    QPushButton *park_button_ = nullptr;

    void fill();

    /// Индекс выбранной проблемы (-1 - ничего не выбрано)
    int selectedIssue() const;
};

} // namespace rse

#endif // RSE_CHECK_DIALOG_H
