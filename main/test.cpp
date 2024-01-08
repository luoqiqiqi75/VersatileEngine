#include <iostream>

#include <veCommon>

#include <QApplication>
#include <QPushButton>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QSpinBox>
#include <QElapsedTimer>

int main(int argc, char *argv[])
{
    ve::entry::setup("config.ini");

    QApplication a(argc, argv);

    QElapsedTimer et;
    et.start();

    auto ad = ve::data::at("a");
    for (int i = 0; i < 262144; i++) {
        if (ad->c(0)->getBool()) { qDebug() << 1; }
        if (ad->getBool()) { qDebug() << 1; }
    }

    qDebug() << et.elapsed();
    return 0;

    QWidget w;
    auto l0 = new QVBoxLayout;
    l0->setMargin(0);
    l0->setSpacing(1);
    w.setLayout(l0);

    auto terminal_w = ve::terminal::widget();
    l0->addWidget(terminal_w);

    auto l1 = new QHBoxLayout;
    l1->setMargin(1);
    l1->setSpacing(1);
    l0->addLayout(l1);

    auto prizes_d = ve::data::create(&w, "prizes");

    QSpinBox* sb = new QSpinBox(&w);
    QObject::connect(prizes_d, &ve::Data::changed, sb, [=] {
        sb->setMaximum(prizes_d->size());
    });

    QPushButton* test1_pb = new QPushButton(&w);
    test1_pb->setText("Test 1");
    QObject::connect(test1_pb, &QPushButton::clicked, test1_pb, [=] {
        ILOG << "a.b.c" << " = " << ve::data::at("a", "b", "c")->getDouble(1.1);
        ELOG << "a.b.d" << " = " << ve::data::at("a.b.d")->get(2);
    });
    l1->addWidget(test1_pb);

    QPushButton* test2_pb = new QPushButton(&w);
    test2_pb->setText("Test 2");
    QObject::connect(test2_pb, &QPushButton::clicked, [=] {
        int i = sb->value();
        SLOG << "prizes.#" << i << " = " << ve::data::at("prizes")->c(i)->exportToJson();
        SLOG << (100 / i);
    });
    l1->addWidget(sb);
    l1->addWidget(test2_pb);

    w.show();

    int res = a.exec();

    terminal_w->setParent(nullptr);

    return res;
}
