#include "dynamic_typesupport_bridge.h"

#include "ve/core/var.h"

#include <rcl_interfaces/srv/get_parameters.hpp>
#include <rcl_interfaces/srv/set_logger_levels.hpp>

#include <cstdlib>
#include <iostream>
#include <string>
#include <utility>

namespace {

int fail(const std::string& message)
{
    std::cerr << "dynamic service bridge test failed: " << message << '\n';
    return EXIT_FAILURE;
}

} // namespace

int main()
{
    using Bridge = ve::ros::rclcpp_backend::DynamicTypesupportBridge;
    using Service = rcl_interfaces::srv::SetLoggerLevels;

    Bridge bridge;
    std::string error;
    if (!bridge.initializeService("rcl_interfaces/srv/SetLoggerLevels", error))
        return fail(error);

    ve::Var::DictV logger;
    logger["name"] = ve::Var("kinematics");
    logger["level"] = ve::Var(static_cast<int64_t>(20));

    ve::Var::ListV levels;
    levels.push_back(ve::Var(std::move(logger)));

    ve::Var::DictV request_value;
    request_value["levels"] = ve::Var(std::move(levels));

    auto request = bridge.requestFromVar(ve::Var(std::move(request_value)), error);
    if (!request)
        return fail(error);

    const auto * typed_request = static_cast<const Service::Request *>(request.get());
    if (typed_request->levels.size() != 1)
        return fail("nested request sequence has the wrong size");
    if (typed_request->levels[0].name != "kinematics")
        return fail("C++ request string was not populated");
    if (typed_request->levels[0].level != 20)
        return fail("nested request primitive was not populated");

    Service::Response response;
    rcl_interfaces::msg::SetLoggerLevelsResult result;
    result.successful = false;
    result.reason = "logger rejected";
    response.results.push_back(std::move(result));

    ve::Var response_value;
    if (!bridge.responseToVar(&response, response_value, error))
        return fail(error);
    if (!response_value.isDict())
        return fail("response is not a dict");

    const auto& results = response_value.toDict().value("results");
    if (!results.isList() || results.toList().size() != 1)
        return fail("nested response sequence has the wrong size");

    const auto& decoded = results.toList().front();
    if (!decoded.isDict())
        return fail("nested response is not a dict");
    if (decoded.toDict().value("successful").toBool())
        return fail("nested response boolean was not decoded");
    if (decoded.toDict().value("reason").toString() != "logger rejected")
        return fail("C++ response string was not decoded");

    Bridge array_bridge;
    error.clear();
    if (!array_bridge.initializeService("rcl_interfaces/srv/GetParameters", error))
        return fail(error);

    ve::Var::ListV names;
    names.push_back(ve::Var("first"));
    names.push_back(ve::Var("second"));
    ve::Var::DictV array_request_value;
    array_request_value["names"] = ve::Var(std::move(names));

    auto array_request = array_bridge.requestFromVar(
        ve::Var(std::move(array_request_value)), error);
    if (!array_request)
        return fail(error);

    using ArrayService = rcl_interfaces::srv::GetParameters;
    const auto * typed_array_request =
        static_cast<const ArrayService::Request *>(array_request.get());
    if (typed_array_request->names.size() != 2
        || typed_array_request->names[0] != "first"
        || typed_array_request->names[1] != "second")
        return fail("C++ request string sequence was not populated");

    ArrayService::Response array_response;
    rcl_interfaces::msg::ParameterValue parameter_value;
    parameter_value.string_array_value = {"first", "second"};
    parameter_value.bool_array_value = {true, false};
    array_response.values.push_back(std::move(parameter_value));

    ve::Var array_response_value;
    if (!array_bridge.responseToVar(&array_response, array_response_value, error))
        return fail(error);

    const auto& values = array_response_value.toDict().value("values");
    if (!values.isList() || values.toList().size() != 1)
        return fail("C++ nested response sequence was not decoded");

    const auto& decoded_value = values.toList().front().toDict();
    const auto& decoded_strings = decoded_value.value("string_array_value");
    if (!decoded_strings.isList()
        || decoded_strings.toList().size() != 2
        || decoded_strings.toList()[0].toString() != "first"
        || decoded_strings.toList()[1].toString() != "second")
        return fail("C++ response string sequence was not decoded");

    const auto& decoded_bools = decoded_value.value("bool_array_value");
    if (!decoded_bools.isList()
        || decoded_bools.toList().size() != 2
        || !decoded_bools.toList()[0].toBool()
        || decoded_bools.toList()[1].toBool())
        return fail("C++ response bool sequence was not decoded");

    return EXIT_SUCCESS;
}
