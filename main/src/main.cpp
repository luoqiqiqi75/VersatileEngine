#include <QApplication>
#include <QWidget>

#include <veCommon>

int main(int argc, char *argv[])
{
    veLogI << "\n\n\n==================================================";

    ve::entry::setup("config.ini");

    for (int i = 0; i < argc; i++) {
        ve::d("arg")->append(nullptr)->set(nullptr, QString(argv[i]));
    }

    QApplication a(argc, argv);

    ve::entry::init();

    ve::terminal::widget()->show();

    int res = a.exec();

    ve::entry::deinit();

    return 0;
}
