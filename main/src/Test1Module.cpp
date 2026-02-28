#include "Test1Module.h"

//#include <pthread.h>

#include <QDebug>
#include <QDateTime>
#include <QSharedMemory>

#include <ve/service/compact_binary_service.h>

// VE_REGISTER_MODULE(test1, Test1Module)

using namespace std::chrono;

template<typename TimePoint>
std::string to_string(const TimePoint& time_point)
{
    return std::to_string(time_point.time_since_epoch().count());
}

std::atomic_uint64_t task_cnt = 0;
auto task_t0 = high_resolution_clock::now();
auto task_dt = microseconds(1 * 1000);
QSharedMemory test_mem;
int test_data[100] = {0};

bool rw;

std::thread task_thread;
void write_task()
{
    auto msd_cnt = duration_cast<microseconds>(high_resolution_clock::now() - task_t0).count();
    auto des_cnt = duration_cast<microseconds>(task_dt).count();
    des_cnt *= task_cnt;
    // do some stuff
    test_data[task_cnt % 100] += std::abs(msd_cnt - des_cnt);
    if (task_cnt % 500 == 0) {
        qDebug() << QTime::currentTime() << task_cnt << "des:" << des_cnt << "msd:" << msd_cnt;
    }
    test_data[0] = task_cnt;

//    if (test_mem.lock()) {
        memcpy(test_mem.data(), test_data, sizeof(test_data));
//        test_mem.unlock();
//    } else {
//        qDebug() << "cannot lock mem";
//    }
}

void read_task()
{
    auto msd_cnt = duration_cast<microseconds>(high_resolution_clock::now() - task_t0).count();
    auto des_cnt = duration_cast<microseconds>(task_dt).count();
    des_cnt *= task_cnt;

    static int last_i = -1;
//    test_mem.lock();
    memcpy(test_data, test_mem.constData(), sizeof(test_data));
//    test_mem.unlock();
    if (last_i < 0) {

    } else {
        if (last_i + 1 != test_data[0]) {
            qDebug() << QTime::currentTime() << " cnt: " << task_cnt << ", des:" << des_cnt << ", msd:" << msd_cnt << " - FATAL i: (" << last_i << " -> " << test_data[0] << ")";
        } else if (last_i % 500 == 0) {
            qDebug() << "correct cnt: " << task_cnt << " - i:" << last_i << QTime::currentTime();
        }
    }
    last_i = test_data[0];
}

void Test1Module::init()
{
    rw = ve::d("arg.#1")->getString() == "-r";
    qDebug() << "init with " << (rw ? "read" : "write");

    ve::server::cbs::start();

//    test_mem.setKey("test1");
//    if (rw) {
//        if (!test_mem.attach(QSharedMemory::ReadOnly)) {
//            qDebug() << "shared mem attach failed";
//        } else {
//            qDebug() << "shared mem attached";
//        }
//    } else {
//        if (!test_mem.create(sizeof(test_data))) {
//            qDebug() << "shared mem create failed" << test_mem.error();
////            if (test_mem.error() == QSharedMemory::AlreadyExists) {
//                test_mem.attach();
//                test_mem.detach();
//                if (!test_mem.create(sizeof(test_data))) {
//                    qDebug() << "really failed";
//                } else {
//                    qDebug() << "recreate success";
//                }
////            }
//        } else {
//            qDebug() << "shared mem create success";
//        }
//    }
//
//    task_thread = std::thread([] {
//        auto max_t = seconds(rw ? 3 : 10);
//        std::uint64_t max_task_cnt = duration_cast<microseconds>(max_t).count() / duration_cast<microseconds>(task_dt).count();
//
//        if (rw) {
//            std::this_thread::sleep_until(round<seconds>(task_t0) + seconds(1) + microseconds(20));
//        } else {
//            std::this_thread::sleep_until(round<seconds>(task_t0) + seconds(1));
//        }
//        task_t0 = high_resolution_clock::now();
//
//        sched_param sch;
//        int policy;
//        pthread_getschedparam(pthread_self(), &policy, &sch);
//
//        qDebug() << "start at: " << QTime::currentTime() << " executando na prioridade: " << sch.sched_priority;
//
//        const bool rwi = rw;
//        forever {
//            if (rwi) {
//                read_task();
//            } else {
//                write_task();
//            }
//            if (++task_cnt == max_task_cnt) {
//                qDebug() << "stop at: " << QTime::currentTime();
//                break;
//            }
//            auto t1 = task_t0 + microseconds(task_cnt * duration_cast<microseconds>(task_dt).count());
//            while (high_resolution_clock::now() < t1) {
////                std::this_thread::sleep_for(microseconds(10));
//            }
////            std::this_thread::sleep_until(t1);
//        }
//    });
//
//    sched_param sch;
//    int policy;
//    pthread_getschedparam(task_thread.native_handle(), &policy, &sch);
//    sch.sched_priority = 99;
//    if (pthread_setschedparam(task_thread.native_handle(), SCHED_FIFO, &sch)) {
//        std::cout << "setschedparam falhou: " << strerror(errno) << '\n';
//    }
//
//    task_thread.join();
//
//    test_mem.detach();
}

void Test1Module::ready()
{
}