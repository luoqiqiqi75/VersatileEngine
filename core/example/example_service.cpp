// example_service.cpp — Start HTTP/WS/TCP services with sample Node data
//
// Populates a realistic Node tree and starts all VE services.
// Use this as the C++ backend when developing the H5 frontend:
//
//   1. Build and run this example
//   2. cd js && pnpm dev          (Vite dev server on :5173, proxies /api to :8080)
//   3. Open http://localhost:5173

#include <ve/core/node.h>
#include <ve/core/command.h>
#include <ve/core/log.h>
#include <ve/service/service.h>
#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>

using namespace ve;

static void populateSampleData()
{
    // robot arm model
    n("robot/name")->set(Var("VE-Arm-6DOF"));
    n("robot/status")->set(Var("idle"));
    n("robot/arm/joint1")->set(Var(45.0));
    n("robot/arm/joint2")->set(Var(90.0));
    n("robot/arm/joint3")->set(Var(-30.0));
    n("robot/arm/joint4")->set(Var(0.0));
    n("robot/arm/joint5")->set(Var(15.0));
    n("robot/arm/joint6")->set(Var(-5.0));
    n("robot/arm/speed")->set(Var(100));
    n("robot/gripper/state")->set(Var("open"));
    n("robot/gripper/force")->set(Var(10.5));

    // sensors
    n("sensors/temperature")->set(Var(36.5));
    n("sensors/pressure")->set(Var(1013.25));
    n("sensors/humidity")->set(Var(45.0));
    n("sensors/imu/roll")->set(Var(0.1));
    n("sensors/imu/pitch")->set(Var(-0.05));
    n("sensors/imu/yaw")->set(Var(180.0));

    // system config
    n("config/version")->set(Var("2.0.0"));
    n("config/debug")->set(Var(true));
    n("config/log_level")->set(Var("info"));
    n("config/network/ip")->set(Var("192.168.1.100"));
    n("config/network/port")->set(Var(8080));

    // data table (overlapping children)
    auto* table = n("data/measurements");
    for (int i = 0; i < 5; ++i) {
        auto* row = table->append("sample", i);
        row->set(Var(20.0 + i * 1.5));
    }

    veLogI << "Sample data populated: " << node::root()->dump();
}

static void registerSampleCommands()
{
    command::reg("robot.home", [](Node*) -> Result {
        n("robot/arm/joint1")->set(Var(0.0));
        n("robot/arm/joint2")->set(Var(0.0));
        n("robot/arm/joint3")->set(Var(0.0));
        n("robot/arm/joint4")->set(Var(0.0));
        n("robot/arm/joint5")->set(Var(0.0));
        n("robot/arm/joint6")->set(Var(0.0));
        n("robot/status")->set(Var("homed"));
        return Result::SUCCESS;
    });

    command::reg("robot.move", [](Node* n) -> Result {
        double target = n->get<double>();
        ve::n("robot/arm/joint1")->set(Var(target));
        ve::n("robot/status")->set(Var("moving"));
        return Result(Result::SUCCESS, Var(target));
    });
}

int main()
{
    std::cout << "VersatileEngine Service Example" << std::endl;
    std::cout << "  HTTP server:     http://localhost:8080" << std::endl;
    std::cout << "  WebSocket:       ws://localhost:8081" << std::endl;
    std::cout << "  Terminal (TCP):  telnet localhost 5061" << std::endl;
    std::cout << std::endl;

    populateSampleData();
    registerSampleCommands();

    service::startAll(node::root());

    std::cout << "Services started. Press Ctrl+C to stop." << std::endl;

    std::atomic<bool> running{true};
    while (running) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    service::stopAll();
    return 0;
}
