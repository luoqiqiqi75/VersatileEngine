#include "ExampleModule.h"

#include "ve/core/command.h"
#include "ve/core/log.h"
#include "ve/core/node.h"

VE_REGISTER_MODULE(ve.example, ExampleModule)

ExampleModule::ExampleModule(const std::string& name) : Module(name) {}

void ExampleModule::init()
{
    veLogI << "[ve.example] init";
}

void ExampleModule::ready()
{
    using namespace ve;

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

    n("sensors/temperature")->set(Var(36.5));
    n("sensors/pressure")->set(Var(1013.25));
    n("sensors/humidity")->set(Var(45.0));

    n("config/version")->set(Var("2.0.0"));
    n("config/debug")->set(Var(true));

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

    veLogI << "[ve.example] sample data populated";
}

void ExampleModule::deinit()
{
    veLogI << "[ve.example] deinit";
}
