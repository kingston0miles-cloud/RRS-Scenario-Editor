#include    <QApplication>
#include    <QSettings>
#include    <QTranslator>

#include    "ui/main-window.h"

//------------------------------------------------------------------------------
// Язык интерфейса (этап 30): из настроек, "en" - английский перевод из ресурса
//------------------------------------------------------------------------------
namespace
{

QTranslator *installLanguage()
{
    QSettings settings;
    const QString lang = settings.value(QStringLiteral("ui/language"), QStringLiteral("ru"))
                            .toString();

    if (lang == QLatin1String("en"))
    {
        auto *translator = new QTranslator(QApplication::instance());

        if (translator->load(QStringLiteral(":/translations/rse-editor_en")))
        {
            QApplication::installTranslator(translator);
            return translator;
        }

        delete translator;
    }

    return nullptr;
}

} // namespace

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    QApplication::setApplicationName(QStringLiteral("rrs-scenario-editor"));
    QApplication::setApplicationVersion(QStringLiteral("0.1.0"));

    installLanguage();

    rse::MainWindow window;
    window.show();

    // Необязательный аргумент: каталог маршрута для немедленного открытия
    const QStringList args = QApplication::arguments();

    if (args.size() > 1)
    {
        window.openRoute(args.at(1));
    }

    return QApplication::exec();
}
