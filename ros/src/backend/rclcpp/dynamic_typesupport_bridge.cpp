#include "dynamic_typesupport_bridge.h"

#include <rclcpp/typesupport_helpers.hpp>

#include <rmw/rmw.h>

#include <rosidl_runtime_c/string.h>
#include <rosidl_runtime_c/string_functions.h>
#include <rosidl_runtime_c/u16string.h>
#include <rosidl_runtime_c/u16string_functions.h>
#include <rosidl_typesupport_introspection_c/field_types.h>
#include <rosidl_typesupport_introspection_c/message_introspection.h>

#include <rcpputils/shared_library.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

namespace ve::ros::rclcpp_backend {

namespace {

using CreateMessageFn = void *(*)();
using DestroyMessageFn = void (*)(void *);

std::string normalizeTypeForCSymbol(std::string text)
{
    std::replace(text.begin(), text.end(), '/', '_');
    return text;
}

template<typename T>
const T * fieldPtr(const void * message, uint32_t offset)
{
    return reinterpret_cast<const T *>(static_cast<const uint8_t *>(message) + offset);
}

Var primitiveToVar(uint8_t type_id, const void * value_ptr)
{
    switch (type_id) {
    case rosidl_typesupport_introspection_c__ROS_TYPE_FLOAT:
        return Var(static_cast<double>(*reinterpret_cast<const float *>(value_ptr)));
    case rosidl_typesupport_introspection_c__ROS_TYPE_DOUBLE:
        return Var(*reinterpret_cast<const double *>(value_ptr));
    case rosidl_typesupport_introspection_c__ROS_TYPE_LONG_DOUBLE:
        return Var(static_cast<double>(*reinterpret_cast<const long double *>(value_ptr)));
    case rosidl_typesupport_introspection_c__ROS_TYPE_CHAR:
        return Var(std::string(1, *reinterpret_cast<const char *>(value_ptr)));
    case rosidl_typesupport_introspection_c__ROS_TYPE_WCHAR:
        return Var(static_cast<int64_t>(*reinterpret_cast<const char16_t *>(value_ptr)));
    case rosidl_typesupport_introspection_c__ROS_TYPE_BOOLEAN:
        return Var(*reinterpret_cast<const bool *>(value_ptr));
    case rosidl_typesupport_introspection_c__ROS_TYPE_OCTET:
    case rosidl_typesupport_introspection_c__ROS_TYPE_UINT8:
        return Var(static_cast<int64_t>(*reinterpret_cast<const uint8_t *>(value_ptr)));
    case rosidl_typesupport_introspection_c__ROS_TYPE_INT8:
        return Var(static_cast<int64_t>(*reinterpret_cast<const int8_t *>(value_ptr)));
    case rosidl_typesupport_introspection_c__ROS_TYPE_UINT16:
        return Var(static_cast<int64_t>(*reinterpret_cast<const uint16_t *>(value_ptr)));
    case rosidl_typesupport_introspection_c__ROS_TYPE_INT16:
        return Var(static_cast<int64_t>(*reinterpret_cast<const int16_t *>(value_ptr)));
    case rosidl_typesupport_introspection_c__ROS_TYPE_UINT32:
        return Var(static_cast<int64_t>(*reinterpret_cast<const uint32_t *>(value_ptr)));
    case rosidl_typesupport_introspection_c__ROS_TYPE_INT32:
        return Var(static_cast<int64_t>(*reinterpret_cast<const int32_t *>(value_ptr)));
    case rosidl_typesupport_introspection_c__ROS_TYPE_UINT64:
        return Var(static_cast<int64_t>(*reinterpret_cast<const uint64_t *>(value_ptr)));
    case rosidl_typesupport_introspection_c__ROS_TYPE_INT64:
        return Var(static_cast<int64_t>(*reinterpret_cast<const int64_t *>(value_ptr)));
    default:
        return Var();
    }
}

Var stringToVar(uint8_t type_id, const void * value_ptr)
{
    if (type_id == rosidl_typesupport_introspection_c__ROS_TYPE_STRING) {
        const auto * text = reinterpret_cast<const rosidl_runtime_c__String *>(value_ptr);
        return Var(text && text->data ? std::string(text->data, text->size) : std::string());
    }
    if (type_id == rosidl_typesupport_introspection_c__ROS_TYPE_WSTRING) {
        const auto * text = reinterpret_cast<const rosidl_runtime_c__U16String *>(value_ptr);
        Var::ListV list;
        if (text && text->data) {
            for (size_t i = 0; i < text->size; ++i)
                list.push_back(Var(static_cast<int64_t>(text->data[i])));
        }
        return Var(std::move(list));
    }
    return Var();
}

Var messageToVar(const void * message,
                 const rosidl_typesupport_introspection_c__MessageMembers * members);

bool assignPrimitive(uint8_t type_id, void * target_ptr, const Var& value, std::string& error)
{
    switch (type_id) {
    case rosidl_typesupport_introspection_c__ROS_TYPE_FLOAT:
        *reinterpret_cast<float *>(target_ptr) = static_cast<float>(value.toDouble());
        return true;
    case rosidl_typesupport_introspection_c__ROS_TYPE_DOUBLE:
        *reinterpret_cast<double *>(target_ptr) = value.toDouble();
        return true;
    case rosidl_typesupport_introspection_c__ROS_TYPE_LONG_DOUBLE:
        *reinterpret_cast<long double *>(target_ptr) = static_cast<long double>(value.toDouble());
        return true;
    case rosidl_typesupport_introspection_c__ROS_TYPE_CHAR: {
        const auto text = value.toString();
        *reinterpret_cast<char *>(target_ptr) = text.empty() ? '\0' : text.front();
        return true;
    }
    case rosidl_typesupport_introspection_c__ROS_TYPE_WCHAR:
        *reinterpret_cast<char16_t *>(target_ptr) = static_cast<char16_t>(value.toInt64());
        return true;
    case rosidl_typesupport_introspection_c__ROS_TYPE_BOOLEAN:
        *reinterpret_cast<bool *>(target_ptr) = value.toBool();
        return true;
    case rosidl_typesupport_introspection_c__ROS_TYPE_OCTET:
    case rosidl_typesupport_introspection_c__ROS_TYPE_UINT8:
        *reinterpret_cast<uint8_t *>(target_ptr) = static_cast<uint8_t>(value.toInt64());
        return true;
    case rosidl_typesupport_introspection_c__ROS_TYPE_INT8:
        *reinterpret_cast<int8_t *>(target_ptr) = static_cast<int8_t>(value.toInt64());
        return true;
    case rosidl_typesupport_introspection_c__ROS_TYPE_UINT16:
        *reinterpret_cast<uint16_t *>(target_ptr) = static_cast<uint16_t>(value.toInt64());
        return true;
    case rosidl_typesupport_introspection_c__ROS_TYPE_INT16:
        *reinterpret_cast<int16_t *>(target_ptr) = static_cast<int16_t>(value.toInt64());
        return true;
    case rosidl_typesupport_introspection_c__ROS_TYPE_UINT32:
        *reinterpret_cast<uint32_t *>(target_ptr) = static_cast<uint32_t>(value.toInt64());
        return true;
    case rosidl_typesupport_introspection_c__ROS_TYPE_INT32:
        *reinterpret_cast<int32_t *>(target_ptr) = static_cast<int32_t>(value.toInt64());
        return true;
    case rosidl_typesupport_introspection_c__ROS_TYPE_UINT64:
        *reinterpret_cast<uint64_t *>(target_ptr) = static_cast<uint64_t>(value.toInt64());
        return true;
    case rosidl_typesupport_introspection_c__ROS_TYPE_INT64:
        *reinterpret_cast<int64_t *>(target_ptr) = static_cast<int64_t>(value.toInt64());
        return true;
    default:
        error = "unsupported primitive field type";
        return false;
    }
}

bool assignString(uint8_t type_id, void * target_ptr, const Var& value, std::string& error)
{
    if (type_id == rosidl_typesupport_introspection_c__ROS_TYPE_STRING)
        return rosidl_runtime_c__String__assign(reinterpret_cast<rosidl_runtime_c__String *>(target_ptr),
                                                value.toString().c_str());
    if (type_id == rosidl_typesupport_introspection_c__ROS_TYPE_WSTRING)
        return rosidl_runtime_c__U16String__assignn_from_char(
            reinterpret_cast<rosidl_runtime_c__U16String *>(target_ptr),
            value.toString().c_str(),
            value.toString().size());
    error = "unsupported string field type";
    return false;
}

bool varToMessage(const Var& value,
                  void * message,
                  const rosidl_typesupport_introspection_c__MessageMembers * members,
                  std::string& error);

bool assignArrayElement(void * member_data,
                        const rosidl_typesupport_introspection_c__MessageMember & member,
                        size_t index,
                        const Var& value,
                        std::string& error)
{
    void * item_ptr = member.get_function ? member.get_function(member_data, index) : nullptr;
    if (!item_ptr) {
        error = "array element access failed";
        return false;
    }

    if (member.type_id_ == rosidl_typesupport_introspection_c__ROS_TYPE_MESSAGE) {
        const auto * nested =
            static_cast<const rosidl_typesupport_introspection_c__MessageMembers *>(member.members_->data);
        return varToMessage(value, item_ptr, nested, error);
    }
    if (member.type_id_ == rosidl_typesupport_introspection_c__ROS_TYPE_STRING ||
        member.type_id_ == rosidl_typesupport_introspection_c__ROS_TYPE_WSTRING)
        return assignString(member.type_id_, item_ptr, value, error);
    return assignPrimitive(member.type_id_, item_ptr, value, error);
}

bool assignField(const Var::DictV& dict,
                 void * message,
                 const rosidl_typesupport_introspection_c__MessageMember & member,
                 std::string& error)
{
    auto it = dict.find(member.name_);
    if (it == dict.end())
        return true;

    void * member_ptr = static_cast<uint8_t *>(message) + member.offset_;
    const Var& value = it->second;

    if (member.is_array_) {
        if (!value.isList()) {
            error = std::string("field '") + member.name_ + "' expects list";
            return false;
        }

        const auto & list = value.toList();
        if (member.resize_function) {
            if (!member.resize_function(member_ptr, list.size())) {
                error = std::string("resize failed for field '") + member.name_ + "'";
                return false;
            }
        } else if (list.size() > member.array_size_) {
            error = std::string("too many elements for fixed array field '") + member.name_ + "'";
            return false;
        }

        const size_t limit = member.resize_function ? list.size() : std::min(list.size(), member.array_size_);
        for (size_t i = 0; i < limit; ++i) {
            if (!assignArrayElement(member_ptr, member, i, list[i], error))
                return false;
        }
        return true;
    }

    if (member.type_id_ == rosidl_typesupport_introspection_c__ROS_TYPE_MESSAGE) {
        const auto * nested =
            static_cast<const rosidl_typesupport_introspection_c__MessageMembers *>(member.members_->data);
        return varToMessage(value, member_ptr, nested, error);
    }
    if (member.type_id_ == rosidl_typesupport_introspection_c__ROS_TYPE_STRING ||
        member.type_id_ == rosidl_typesupport_introspection_c__ROS_TYPE_WSTRING)
        return assignString(member.type_id_, member_ptr, value, error);
    return assignPrimitive(member.type_id_, member_ptr, value, error);
}

bool varToMessage(const Var& value,
                  void * message,
                  const rosidl_typesupport_introspection_c__MessageMembers * members,
                  std::string& error)
{
    if (!message || !members) {
        error = "message or introspection metadata is null";
        return false;
    }
    if (!value.isDict()) {
        error = "message payload must be a dict";
        return false;
    }

    const auto & dict = value.toDict();
    for (uint32_t i = 0; i < members->member_count_; ++i) {
        const auto & member = members->members_[i];
        if (!member.name_)
            continue;
        if (member.name_[0] == '_')
            continue;
        if (!assignField(dict, message, member, error))
            return false;
    }
    return true;
}

Var arrayToVar(const void * member_data,
               const rosidl_typesupport_introspection_c__MessageMember & member)
{
    Var::ListV list;
    if (!member.size_function)
        return Var(std::move(list));

    const size_t size = member.size_function(member_data);
    list.reserve(size);
    for (size_t i = 0; i < size; ++i) {
        const void * item_ptr = member.get_const_function
            ? member.get_const_function(member_data, i)
            : nullptr;
        if (!item_ptr) {
            list.push_back(Var());
            continue;
        }

        if (member.type_id_ == rosidl_typesupport_introspection_c__ROS_TYPE_MESSAGE) {
            const auto * nested =
                static_cast<const rosidl_typesupport_introspection_c__MessageMembers *>(member.members_->data);
            list.push_back(messageToVar(item_ptr, nested));
        } else if (member.type_id_ == rosidl_typesupport_introspection_c__ROS_TYPE_STRING ||
                   member.type_id_ == rosidl_typesupport_introspection_c__ROS_TYPE_WSTRING) {
            list.push_back(stringToVar(member.type_id_, item_ptr));
        } else {
            list.push_back(primitiveToVar(member.type_id_, item_ptr));
        }
    }
    return Var(std::move(list));
}

Var fieldToVar(const void * message,
               const rosidl_typesupport_introspection_c__MessageMember & member)
{
    const void * member_ptr = static_cast<const uint8_t *>(message) + member.offset_;

    if (member.is_array_)
        return arrayToVar(member_ptr, member);

    if (member.type_id_ == rosidl_typesupport_introspection_c__ROS_TYPE_MESSAGE) {
        const auto * nested =
            static_cast<const rosidl_typesupport_introspection_c__MessageMembers *>(member.members_->data);
        return messageToVar(member_ptr, nested);
    }

    if (member.type_id_ == rosidl_typesupport_introspection_c__ROS_TYPE_STRING ||
        member.type_id_ == rosidl_typesupport_introspection_c__ROS_TYPE_WSTRING)
        return stringToVar(member.type_id_, member_ptr);

    return primitiveToVar(member.type_id_, member_ptr);
}

Var messageToVar(const void * message,
                 const rosidl_typesupport_introspection_c__MessageMembers * members)
{
    Var::DictV dict;
    if (!message || !members)
        return Var(std::move(dict));

    for (uint32_t i = 0; i < members->member_count_; ++i) {
        const auto & member = members->members_[i];
        if (!member.name_)
            continue;
        dict[member.name_] = fieldToVar(message, member);
    }
    return Var(std::move(dict));
}

} // namespace

struct DynamicTypesupportBridge::Private
{
    std::shared_ptr<rcpputils::SharedLibrary> message_ts_lib;
    std::shared_ptr<rcpputils::SharedLibrary> introspection_lib;
    std::shared_ptr<rcpputils::SharedLibrary> generator_c_lib;

    const rosidl_message_type_support_t * message_ts = nullptr;
    const rosidl_message_type_support_t * introspection_ts = nullptr;
    const rosidl_typesupport_introspection_c__MessageMembers * introspection_members = nullptr;

    CreateMessageFn create_message = nullptr;
    DestroyMessageFn destroy_message = nullptr;
};

DynamicTypesupportBridge::DynamicTypesupportBridge(const std::string& type, std::string& error)
{
    initialize(type, error);
}

bool DynamicTypesupportBridge::initialize(const std::string& type, std::string& error)
{
    ready_ = false;
    type_ = type;
    p_ = std::make_shared<Private>();

    if (type.empty()) {
        error = "type is required";
        return false;
    }

    std::string package_name;
    std::string middle_module;
    std::string type_name;
    try {
        std::tie(package_name, middle_module, type_name) = rclcpp::extract_type_identifier(type);
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }

    try {
        p_->message_ts_lib = rclcpp::get_typesupport_library(type, "rosidl_typesupport_c");
        p_->message_ts = rclcpp::get_message_typesupport_handle(type, "rosidl_typesupport_c", *p_->message_ts_lib);

        p_->introspection_lib = rclcpp::get_typesupport_library(type, "rosidl_typesupport_introspection_c");
        p_->introspection_ts = rclcpp::get_message_typesupport_handle(
            type, "rosidl_typesupport_introspection_c", *p_->introspection_lib);
        p_->introspection_members =
            static_cast<const rosidl_typesupport_introspection_c__MessageMembers *>(p_->introspection_ts->data);

        const std::string generator_lib_path =
            rclcpp::get_typesupport_library_path(package_name, "rosidl_generator_c");
        p_->generator_c_lib = std::make_shared<rcpputils::SharedLibrary>(generator_lib_path);

        const std::string symbol_base = normalizeTypeForCSymbol(package_name + "__" + middle_module + "__" + type_name);
        p_->create_message = reinterpret_cast<CreateMessageFn>(
            p_->generator_c_lib->get_symbol(symbol_base + "__create"));
        p_->destroy_message = reinterpret_cast<DestroyMessageFn>(
            p_->generator_c_lib->get_symbol(symbol_base + "__destroy"));
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }

    if (!p_->message_ts || !p_->introspection_ts || !p_->introspection_members ||
        !p_->create_message || !p_->destroy_message) {
        error = "failed to initialize message bridge for type: " + type;
        return false;
    }

    ready_ = true;
    return true;
}

bool DynamicTypesupportBridge::deserializeToVar(const rclcpp::SerializedMessage& message,
                                                ve::Var& out,
                                                std::string& error) const
{
    if (!ready_ || !p_) {
        error = "bridge is not initialized";
        return false;
    }

    void * ros_message = p_->create_message();
    if (!ros_message) {
        error = "failed to allocate message instance";
        return false;
    }

    const auto & serialized = message.get_rcl_serialized_message();
    rmw_serialized_message_t rmw_message = serialized;

    const rmw_ret_t rc = rmw_deserialize(&rmw_message, p_->message_ts, ros_message);
    if (rc != RMW_RET_OK) {
        p_->destroy_message(ros_message);
        error = "rmw_deserialize failed";
        return false;
    }

    out = messageToVar(ros_message, p_->introspection_members);
    p_->destroy_message(ros_message);
    return true;
}

bool DynamicTypesupportBridge::serializeFromVar(const ve::Var& value,
                                                rclcpp::SerializedMessage& out,
                                                std::string& error) const
{
    if (!ready_ || !p_) {
        error = "bridge is not initialized";
        return false;
    }

    void * ros_message = p_->create_message();
    if (!ros_message) {
        error = "failed to allocate message instance";
        return false;
    }

    if (!varToMessage(value, ros_message, p_->introspection_members, error)) {
        p_->destroy_message(ros_message);
        return false;
    }

    auto & rmw_message = out.get_rcl_serialized_message();
    const rmw_ret_t rc = rmw_serialize(ros_message, p_->message_ts, &rmw_message);
    p_->destroy_message(ros_message);
    if (rc != RMW_RET_OK) {
        error = "rmw_serialize failed";
        return false;
    }
    return true;
}

} // namespace ve::ros::rclcpp_backend
