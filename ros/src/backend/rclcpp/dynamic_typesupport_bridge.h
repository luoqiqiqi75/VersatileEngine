#pragma once

#include "ve/core/var.h"

#include <rclcpp/serialized_message.hpp>

#include <memory>
#include <string>

namespace ve::ros::rclcpp_backend {

class DynamicTypesupportBridge
{
public:
    DynamicTypesupportBridge() = default;
    explicit DynamicTypesupportBridge(const std::string& type, std::string& error);

    bool initialize(const std::string& type, std::string& error);
    bool isReady() const { return ready_; }
    const std::string& type() const { return type_; }

    bool deserializeToVar(const rclcpp::SerializedMessage& message,
                          ve::Var& out,
                          std::string& error) const;
    bool serializeFromVar(const ve::Var& value,
                          rclcpp::SerializedMessage& out,
                          std::string& error) const;

private:
    bool ready_ = false;
    std::string type_;

    struct Private;
    std::shared_ptr<Private> p_;
};

} // namespace ve::ros::rclcpp_backend
