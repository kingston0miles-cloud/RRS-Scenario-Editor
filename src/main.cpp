#include    <QApplication>

#include    "ui/main-window.h"

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    QApplication::setApplicationName(QStringLiteral("rrs-scenario-editor"));
    QApplication::setApplicationVersion(QStringLiteral("0.1.0"));

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
