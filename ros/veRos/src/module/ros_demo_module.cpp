// ros_demo_module.cpp  - ve::RosDemoModule (ve.ros.demo)
//
// Demonstrates DDS functionality:
//   - Publishes a heartbeat counter on DDS topic "ve/demo/heartbeat"
//   - Registers demo commands accessible via DDS CommandService
//   - Exposes Node subtree to DDS via Bridge

#include "ve/core/module.h"
#include "ve/core/log.h"
#include "ve/core/command.h"
#include "ve/ros/dds/participant.h"
#include "ve/ros/dds/dynamic.h"
#include "ve/ros/dds/bridge.h"

namespace ve {

class RosDemoModule : public Module
{
    std::unique_ptr<dds::Bridge> bridge_;
    int heartbeatCount_ = 0;

public:
    explicit RosDemoModule(const std::string& name) : Module(name)
    {
        veLogI << "[ve.ros.demo] constructor  - preparing demo data";

        n("app/ros_demo/robot/name")->set(Var("ve_arm_1"));
        n("app/ros_demo/robot/x")->set(Var(0.0));
        n("app/ros_demo/robot/y")->set(Var(0.0));
        n("app/ros_demo/robot/z")->set(Var(0.0));
        n("app/ros_demo/robot/active")->set(Var(true));

        n("app/ros_demo/heartbeat")->set(Var(0));
        n("app/ros_demo/status")->set(Var("starting"));
    }

protected:
    void init() override
    {
        veLogI << "[ve.ros.demo] init  - registering commands & DDS bridge";

        command::reg("ros_demo.heartbeat", [this](const Var&) -> Result {
            heartbeatCount_++;
            n("app/ros_demo/heartbeat")->set(Var(heartbeatCount_));
            return Result(Result::SUCCESS, Var(heartbeatCount_));
        }, "Increment and return heartbeat counter");

        command::reg("ros_demo.move", [](const Var& input) -> Result {
            auto* root = n("app/ros_demo/robot");
            if (input.type() == Var::LIST) {
                auto& list = input.toList();
                if (list.size() >= 3) {
                    root->child("x")->set(list[0]);
                    root->child("y")->set(list[1]);
                    root->child("z")->set(list[2]);
                    return Result(Result::SUCCESS, Var("moved"));
                }
            }
            return Result(Result::FAIL, Var("usage: ros_demo.move [x, y, z]"));
        }, "Move robot to [x, y, z]");

        command::reg("ros_demo.status", [](const Var&) -> Result {
            auto* root = n("app/ros_demo");
            std::string s = "robot=" + root->child("robot")->child("name")->get<std::string>()
                + " pos=[" + root->child("robot")->child("x")->value().toString()
                + "," + root->child("robot")->child("y")->value().toString()
                + "," + root->child("robot")->child("z")->value().toString()
                + "] hb=" + root->child("heartbeat")->value().toString();
            return Result(Result::SUCCESS, Var(s));
        }, "Show ros_demo status summary");

        auto& p = dds::Participant::instance();
        bridge_ = std::make_unique<dds::Bridge>(n("app/ros_demo"), p);

        bridge_->expose("robot", "ve/demo/robot");
        bridge_->exposeCommand("ros_demo.heartbeat", "ve/demo/heartbeat_srv");
        bridge_->exposeCommand("ros_demo.status", "ve/demo/status_srv");

        veLogI << "[ve.ros.demo] DDS bridge: exposed "
               << bridge_->exposedCount() << " topic(s), "
               << bridge_->subscribedCount() << " subscription(s)";
    }

    void ready() override
    {
        n("app/ros_demo/status")->set(Var("ready"));
        veLogI << "[ve.ros.demo] ready  - try commands: ros_demo.heartbeat, ros_demo.move, ros_demo.status";
    }

    void deinit() override
    {
        bridge_.reset();
        n("app/ros_demo/status")->set(Var("stopped"));
        veLogI << "[ve.ros.demo] deinit";
    }
};

} // namespace ve

VE_REGISTER_PRIORITY_MODULE(ve.ros.demo, ve::RosDemoModule, 90, 1)
