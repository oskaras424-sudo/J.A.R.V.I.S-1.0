#include "MainWindow.h"

#include <QApplication>
#include <QFont>
#include <QFontDatabase>
#include <QPalette>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("JarvisAssistant");
    QCoreApplication::setApplicationName("JarvisAssistant");
    QCoreApplication::setApplicationVersion("0.1.0");

    QFont font = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
    font.setPointSize(10);
    app.setFont(font);

    MainWindow window;
    window.show();
    return app.exec();
}
