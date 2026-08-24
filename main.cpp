// #include "demodialog.h"

#include <QApplication>
#include "ckernal.h"
#include "appstyle.h"

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    a.setApplicationName("Audio-Video");
    a.setApplicationDisplayName("Audio-Video");
    applyAppStyle(a);
//    demoDialog w;
//    w.show();
    CKernal::getInstance();
    return a.exec();
}
