// #include "demodialog.h"

#include <QApplication>
#include "ckernal.h"
#include "appstyle.h"

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    applyAppStyle(a);
//    demoDialog w;
//    w.show();
    CKernal::getInstance();
    return a.exec();
}
