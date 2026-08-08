#include "ui/main_window.h"

#include <QApplication>
#include <QCoreApplication>

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("VectorTypographyEditor"));
    QCoreApplication::setOrganizationName(QStringLiteral("VectorTypography"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));

    vt::MainWindow window;
    window.show();
    return application.exec();
}
