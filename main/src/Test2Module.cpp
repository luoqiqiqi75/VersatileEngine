//
// Created by lqi on 2025/9/26.
//

#include "Test2Module.h"

#include <QElapsedTimer>
#include <QThreadPool>

#include "ve/service/compact_binary_service.h"

VE_REGISTER_MODULE(test2, Test2Module)

// struct Point {
//     float x;  // 4字节
//     float y;  // 4字节
//     float z;  // 4字节
//     uint8_t intensity;  // 1字节（共13字节/点，向上对齐到16字节）
// };
// struct PointCloud {
//     uint32_t width;       // 4字节
//     uint32_t height;      // 4字节
//     Point points[65536];  // 65536点 × 16字节 = 1,048,576字节（1MB）
// };

void testRecv()
{
    ve::server::cbs::start();

    auto data_d = ve::d("test.data");
    auto status_d = ve::d("test.status");

    static QElapsedTimer et;
    static int cnt = 0;
    QObject::connect(status_d, &ve::Data::changed, [&] (const QVariant& var) {
        if (var.toBool()) {
            veLogIs << "start to recv";
            cnt = 0;
            et.start();
        } else {
            veLogIs << "[recv fin] elapsed: " << et.elapsed() << ", cnt: " << cnt << ", rate = " << (cnt * 1000.0 / et.elapsed());
        }
    });
    QObject::connect(data_d, &ve::Data::changed, [&] {
        cnt++;
    });
}

void testSend()
{
    ve::client::cbs::connectTo("127.0.0.1", 5065);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    qDebug() << "testSend";

    auto send_d = ve::d("test.data2");
    int send_cnt = 0;

    // static PointCloud pc[10];
    QByteArray ba(static_cast<int>(ve::d("test.size")->getDouble(1.0) * 1024), '6');
    int timeout = static_cast<int>(ve::d("test.timeout")->getDouble(2.0) * 1000);

    send_d->set(nullptr, QVariant::fromValue(ba));

    veLogI << "start send, size = " << ba.size() / 1024.0 << "kb" << ", delay = " << timeout / 1000.0 << "s";
    send_cnt = 0;
    ve::client::cbs::publish("test.status", true);

    QElapsedTimer et;
    et.start();
    while (!et.hasExpired(timeout)) {
        ve::client::cbs::publish("test.data", send_d);
        send_cnt++;
    }
    veLogIs << "send fin  cnt:" << send_cnt << "time passed " << et.elapsed() << ", tps: " << (1.0 * send_cnt * ba.size() / 1024 / 1024 * 1000 / et.elapsed());

    ve::client::cbs::publish("test.status", QVariant(false));

}

void Test2Module::init()
{
    std::array<unsigned long long, 10> counts {0};
    counts.fill(0);
    auto task_f = [&] (int i) {
        counts[i]++;
    };

    if (false) {
        bool flag = true;
        QThreadPool::globalInstance()->start([&] { while (flag) task_f(0); });
        QThreadPool::globalInstance()->start([&] { while (flag) task_f(1); });

        std::this_thread::sleep_for(std::chrono::seconds(10));
        flag = false;
    }

    if (false) {
        ve::Data test_d("test");
        QObject::connect(&test_d, &ve::Data::changed, &test_d, [&] { task_f(0); });
        QObject::connect(&test_d, &ve::Data::changed, &test_d, [&] { task_f(1); });

        QElapsedTimer et;
        et.start();
        while (!et.hasExpired(10 * 1000)) {
            test_d.trigger();
        }
    }

    if (true) {
        QElapsedTimer et;
        et.start();
        while (!et.hasExpired(20 * 1000)) {
            task_f(0);
            task_f(1);
        }
    }

    qDebug() << "count 1: " << counts[0];
    qDebug() << "count 2: " << counts[1];
}

void Test2Module::ready()
{
    QObject::connect(ve::d("trigger.send"), &ve::Data::changed, []{ testSend(); });
    QObject::connect(ve::d("trigger.recv"), &ve::Data::changed, []{ testRecv(); });
}
