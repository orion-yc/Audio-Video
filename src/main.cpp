// #include "demodialog.h"

#include <QApplication>
#include <QIcon>
#include "ckernal.h"
#include "appstyle.h"
#include "diagnostics.h"

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    a.setApplicationName("Audio-Video");
    a.setApplicationVersion("2.0.2");
    a.setApplicationDisplayName("Audio-Video");
    // Keep the runtime window icon consistent with the packaged executable.
    a.setWindowIcon(QIcon(QStringLiteral(":/images/app-icon.png")));
    applyAppStyle(a);
    DiagnosticLogger::instance().initialize();
//    demoDialog w;
//    w.show();
    CKernal::getInstance();
    return a.exec();
}
