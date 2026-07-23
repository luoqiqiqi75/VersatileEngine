#include "dynamic_typesupport_bridge.h"

#include <rclcpp/typesupport_helpers.hpp>

#include <rmw/rmw.h>

#include <rosidl_runtime_c/string.h>
#include <rosidl_runtime_c/string_functions.h>
#include <rosidl_runtime_c/u16string.h>
#include <rosidl_runtime_c/u16string_functions.h>
#ifdef VE_ROS_HAS_GENERIC_SERVICE
#include <rosidl_runtime_cpp/message_initialization.hpp>
#endif
#include <rosidl_typesupport_introspection_c/field_types.h>
#include <rosidl_typesupport_introspection_c/message_introspection.h>
#ifdef VE_ROS_HAS_GENERIC_SERVICE
#include <rosidl_typesupport_introspection_cpp/field_types.hpp>
#include <rosidl_typesupport_introspection_cpp/message_introspection.hpp>
#endif

#include <rcpputils/shared_library.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <string>
#include <tuple>
#include <utility>
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

#ifdef VE_ROS_HAS_GENERIC_SERVICE

// GenericClient consumes constructed C++ messages, not C generator objects.
using CppMessageMember = rosidl_typesupport_introspection_cpp::MessageMember;
using CppMessageMembers = rosidl_typesupport_introspection_cpp::MessageMembers;

const CppMessageMembers * cppNestedMembers(const CppMessageMember& member)
{
    if (!member.members_ || !member.members_->data)
        return nullptr;
    return static_cast<const CppMessageMembers *>(member.members_->data);
}

Var cppPrimitiveToVar(uint8_t type_id, const void * value_ptr)
{
    using namespace rosidl_typesupport_introspection_cpp;
    switch (type_id) {
    case ROS_TYPE_FLOAT:
        return Var(static_cast<double>(*static_cast<const float *>(value_ptr)));
    case ROS_TYPE_DOUBLE:
        return Var(*static_cast<const double *>(value_ptr));
    case ROS_TYPE_LONG_DOUBLE:
        return Var(static_cast<double>(*static_cast<const long double *>(value_ptr)));
    case ROS_TYPE_CHAR:
        return Var(std::string(1, static_cast<char>(
            *static_cast<const unsigned char *>(value_ptr))));
    case ROS_TYPE_WCHAR:
        return Var(static_cast<int64_t>(*static_cast<const char16_t *>(value_ptr)));
    case ROS_TYPE_BOOLEAN:
        return Var(*static_cast<const bool *>(value_ptr));
    case ROS_TYPE_OCTET:
        return Var(static_cast<int64_t>(
            std::to_integer<uint8_t>(*static_cast<const std::byte *>(value_ptr))));
    case ROS_TYPE_UINT8:
        return Var(static_cast<int64_t>(*static_cast<const uint8_t *>(value_ptr)));
    case ROS_TYPE_INT8:
        return Var(static_cast<int64_t>(*static_cast<const int8_t *>(value_ptr)));
    case ROS_TYPE_UINT16:
        return Var(static_cast<int64_t>(*static_cast<const uint16_t *>(value_ptr)));
    case ROS_TYPE_INT16:
        return Var(static_cast<int64_t>(*static_cast<const int16_t *>(value_ptr)));
    case ROS_TYPE_UINT32:
        return Var(static_cast<int64_t>(*static_cast<const uint32_t *>(value_ptr)));
    case ROS_TYPE_INT32:
        return Var(static_cast<int64_t>(*static_cast<const int32_t *>(value_ptr)));
    case ROS_TYPE_UINT64:
        return Var(static_cast<int64_t>(*static_cast<const uint64_t *>(value_ptr)));
    case ROS_TYPE_INT64:
        return Var(*static_cast<const int64_t *>(value_ptr));
    default:
        return Var();
    }
}

std::u16string cppU16StringFromVar(const Var& value)
{
    std::u16string text;
    if (value.isList()) {
        text.reserve(value.toList().size());
        for (const auto& item : value.toList())
            text.push_back(static_cast<char16_t>(item.toInt64()));
        return text;
    }

    const auto narrow = value.toString();
    text.reserve(narrow.size());
    for (unsigned char ch : narrow)
        text.push_back(static_cast<char16_t>(ch));
    return text;
}

Var cppStringToVar(uint8_t type_id, const void * value_ptr)
{
    using namespace rosidl_typesupport_introspection_cpp;
    if (type_id == ROS_TYPE_STRING)
        return Var(*static_cast<const std::string *>(value_ptr));
    if (type_id == ROS_TYPE_WSTRING) {
        Var::ListV list;
        const auto& text = *static_cast<const std::u16string *>(value_ptr);
        list.reserve(text.size());
        for (char16_t ch : text)
            list.push_back(Var(static_cast<int64_t>(ch)));
        return Var(std::move(list));
    }
    return Var();
}

bool cppAssignPrimitive(uint8_t type_id,
                        void * target_ptr,
                        const Var& value,
                        std::string& error)
{
    using namespace rosidl_typesupport_introspection_cpp;
    switch (type_id) {
    case ROS_TYPE_FLOAT:
        *static_cast<float *>(target_ptr) = static_cast<float>(value.toDouble());
        return true;
    case ROS_TYPE_DOUBLE:
        *static_cast<double *>(target_ptr) = value.toDouble();
        return true;
    case ROS_TYPE_LONG_DOUBLE:
        *static_cast<long double *>(target_ptr) = static_cast<long double>(value.toDouble());
        return true;
    case ROS_TYPE_CHAR: {
        const auto text = value.toString();
        *static_cast<unsigned char *>(target_ptr) = static_cast<unsigned char>(
            text.empty() ? '\0' : text.front());
        return true;
    }
    case ROS_TYPE_WCHAR:
        *static_cast<char16_t *>(target_ptr) = static_cast<char16_t>(value.toInt64());
        return true;
    case ROS_TYPE_BOOLEAN:
        *static_cast<bool *>(target_ptr) = value.toBool();
        return true;
    case ROS_TYPE_OCTET:
        *static_cast<std::byte *>(target_ptr) = static_cast<std::byte>(value.toInt64());
        return true;
    case ROS_TYPE_UINT8:
        *static_cast<uint8_t *>(target_ptr) = static_cast<uint8_t>(value.toInt64());
        return true;
    case ROS_TYPE_INT8:
        *static_cast<int8_t *>(target_ptr) = static_cast<int8_t>(value.toInt64());
        return true;
    case ROS_TYPE_UINT16:
        *static_cast<uint16_t *>(target_ptr) = static_cast<uint16_t>(value.toInt64());
        return true;
    case ROS_TYPE_INT16:
        *static_cast<int16_t *>(target_ptr) = static_cast<int16_t>(value.toInt64());
        return true;
    case ROS_TYPE_UINT32:
        *static_cast<uint32_t *>(target_ptr) = static_cast<uint32_t>(value.toInt64());
        return true;
    case ROS_TYPE_INT32:
        *static_cast<int32_t *>(target_ptr) = static_cast<int32_t>(value.toInt64());
        return true;
    case ROS_TYPE_UINT64:
        *static_cast<uint64_t *>(target_ptr) = static_cast<uint64_t>(value.toInt64());
        return true;
    case ROS_TYPE_INT64:
        *static_cast<int64_t *>(target_ptr) = value.toInt64();
        return true;
    default:
        error = "unsupported C++ primitive field type";
        return false;
    }
}

bool cppAssignString(uint8_t type_id,
                     void * target_ptr,
                     const Var& value,
                     std::string& error)
{
    using namespace rosidl_typesupport_introspection_cpp;
    if (type_id == ROS_TYPE_STRING) {
        *static_cast<std::string *>(target_ptr) = value.toString();
        return true;
    }
    if (type_id == ROS_TYPE_WSTRING) {
        *static_cast<std::u16string *>(target_ptr) = cppU16StringFromVar(value);
        return true;
    }
    error = "unsupported C++ string field type";
    return false;
}

bool cppVarToMessage(const Var& value,
                     void * message,
                     const CppMessageMembers * members,
                     std::string& error);

template<typename T>
bool cppAssignArrayValue(void * member_data,
                         const CppMessageMember& member,
                         size_t index,
                         T converted,
                         std::string& error)
{
    if (member.assign_function) {
        member.assign_function(member_data, index, &converted);
        return true;
    }
    void * item_ptr = member.get_function
        ? member.get_function(member_data, index)
        : nullptr;
    if (!item_ptr) {
        error = "C++ array element access failed";
        return false;
    }
    *static_cast<T *>(item_ptr) = std::move(converted);
    return true;
}

bool cppAssignPrimitiveArrayValue(void * member_data,
                                  const CppMessageMember& member,
                                  size_t index,
                                  const Var& value,
                                  std::string& error)
{
    using namespace rosidl_typesupport_introspection_cpp;
    switch (member.type_id_) {
    case ROS_TYPE_FLOAT:
        return cppAssignArrayValue(member_data, member, index,
                                   static_cast<float>(value.toDouble()), error);
    case ROS_TYPE_DOUBLE:
        return cppAssignArrayValue(member_data, member, index, value.toDouble(), error);
    case ROS_TYPE_LONG_DOUBLE:
        return cppAssignArrayValue(member_data, member, index,
                                   static_cast<long double>(value.toDouble()), error);
    case ROS_TYPE_CHAR: {
        const auto text = value.toString();
        return cppAssignArrayValue(member_data, member, index,
                                   static_cast<unsigned char>(
                                       text.empty() ? '\0' : text.front()), error);
    }
    case ROS_TYPE_WCHAR:
        return cppAssignArrayValue(member_data, member, index,
                                   static_cast<char16_t>(value.toInt64()), error);
    case ROS_TYPE_BOOLEAN:
        return cppAssignArrayValue(member_data, member, index, value.toBool(), error);
    case ROS_TYPE_OCTET:
        return cppAssignArrayValue(member_data, member, index,
                                   static_cast<std::byte>(value.toInt64()), error);
    case ROS_TYPE_UINT8:
        return cppAssignArrayValue(member_data, member, index,
                                   static_cast<uint8_t>(value.toInt64()), error);
    case ROS_TYPE_INT8:
        return cppAssignArrayValue(member_data, member, index,
                                   static_cast<int8_t>(value.toInt64()), error);
    case ROS_TYPE_UINT16:
        return cppAssignArrayValue(member_data, member, index,
                                   static_cast<uint16_t>(value.toInt64()), error);
    case ROS_TYPE_INT16:
        return cppAssignArrayValue(member_data, member, index,
                                   static_cast<int16_t>(value.toInt64()), error);
    case ROS_TYPE_UINT32:
        return cppAssignArrayValue(member_data, member, index,
                                   static_cast<uint32_t>(value.toInt64()), error);
    case ROS_TYPE_INT32:
        return cppAssignArrayValue(member_data, member, index,
                                   static_cast<int32_t>(value.toInt64()), error);
    case ROS_TYPE_UINT64:
        return cppAssignArrayValue(member_data, member, index,
                                   static_cast<uint64_t>(value.toInt64()), error);
    case ROS_TYPE_INT64:
        return cppAssignArrayValue(member_data, member, index, value.toInt64(), error);
    default:
        error = "unsupported C++ primitive array field type";
        return false;
    }
}

bool cppAssignArrayElement(void * member_data,
                           const CppMessageMember& member,
                           size_t index,
                           const Var& value,
                           std::string& error)
{
    using namespace rosidl_typesupport_introspection_cpp;
    if (member.type_id_ == ROS_TYPE_MESSAGE) {
        void * item_ptr = member.get_function
            ? member.get_function(member_data, index)
            : nullptr;
        if (!item_ptr) {
            error = "C++ nested array element access failed";
            return false;
        }
        return cppVarToMessage(value, item_ptr, cppNestedMembers(member), error);
    }
    if (member.type_id_ == ROS_TYPE_STRING)
        return cppAssignArrayValue(member_data, member, index, value.toString(), error);
    if (member.type_id_ == ROS_TYPE_WSTRING)
        return cppAssignArrayValue(member_data, member, index,
                                   cppU16StringFromVar(value), error);
    return cppAssignPrimitiveArrayValue(member_data, member, index, value, error);
}

bool cppAssignField(const Var::DictV& dict,
                    void * message,
                    const CppMessageMember& member,
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

        const auto& list = value.toList();
        if (member.resize_function) {
            if (member.is_upper_bound_ && list.size() > member.array_size_) {
                error = std::string("too many elements for bounded sequence field '")
                    + member.name_ + "'";
                return false;
            }
            member.resize_function(member_ptr, list.size());
        } else if (list.size() > member.array_size_) {
            error = std::string("too many elements for fixed array field '")
                + member.name_ + "'";
            return false;
        }

        const size_t limit = member.resize_function
            ? list.size()
            : std::min(list.size(), member.array_size_);
        for (size_t i = 0; i < limit; ++i) {
            if (!cppAssignArrayElement(member_ptr, member, i, list[i], error))
                return false;
        }
        return true;
    }

    using namespace rosidl_typesupport_introspection_cpp;
    if (member.type_id_ == ROS_TYPE_MESSAGE)
        return cppVarToMessage(value, member_ptr, cppNestedMembers(member), error);
    if (member.type_id_ == ROS_TYPE_STRING || member.type_id_ == ROS_TYPE_WSTRING)
        return cppAssignString(member.type_id_, member_ptr, value, error);
    return cppAssignPrimitive(member.type_id_, member_ptr, value, error);
}

bool cppVarToMessage(const Var& value,
                     void * message,
                     const CppMessageMembers * members,
                     std::string& error)
{
    if (!message || !members) {
        error = "C++ message or introspection metadata is null";
        return false;
    }
    if (!value.isDict()) {
        error = "message payload must be a dict";
        return false;
    }

    const auto& dict = value.toDict();
    for (uint32_t i = 0; i < members->member_count_; ++i) {
        const auto& member = members->members_[i];
        if (!member.name_ || member.name_[0] == '_')
            continue;
        if (!cppAssignField(dict, message, member, error))
            return false;
    }
    return true;
}

template<typename T>
Var cppArrayPrimitiveToVar(const void * member_data,
                           const CppMessageMember& member,
                           size_t index)
{
    T value{};
    if (member.fetch_function) {
        member.fetch_function(member_data, index, &value);
        return cppPrimitiveToVar(member.type_id_, &value);
    }
    const void * item_ptr = member.get_const_function
        ? member.get_const_function(member_data, index)
        : nullptr;
    return item_ptr ? cppPrimitiveToVar(member.type_id_, item_ptr) : Var();
}

Var cppArrayElementToVar(const void * member_data,
                         const CppMessageMember& member,
                         size_t index);

Var cppMessageToVar(const void * message, const CppMessageMembers * members)
{
    Var::DictV dict;
    if (!message || !members)
        return Var(std::move(dict));

    for (uint32_t i = 0; i < members->member_count_; ++i) {
        const auto& member = members->members_[i];
        if (!member.name_)
            continue;

        const void * member_ptr = static_cast<const uint8_t *>(message) + member.offset_;
        if (member.is_array_) {
            Var::ListV list;
            const size_t size = member.size_function
                ? member.size_function(member_ptr)
                : member.array_size_;
            list.reserve(size);
            for (size_t index = 0; index < size; ++index)
                list.push_back(cppArrayElementToVar(member_ptr, member, index));
            dict[member.name_] = Var(std::move(list));
            continue;
        }

        using namespace rosidl_typesupport_introspection_cpp;
        if (member.type_id_ == ROS_TYPE_MESSAGE)
            dict[member.name_] = cppMessageToVar(member_ptr, cppNestedMembers(member));
        else if (member.type_id_ == ROS_TYPE_STRING || member.type_id_ == ROS_TYPE_WSTRING)
            dict[member.name_] = cppStringToVar(member.type_id_, member_ptr);
        else
            dict[member.name_] = cppPrimitiveToVar(member.type_id_, member_ptr);
    }
    return Var(std::move(dict));
}

Var cppArrayElementToVar(const void * member_data,
                         const CppMessageMember& member,
                         size_t index)
{
    using namespace rosidl_typesupport_introspection_cpp;
    if (member.type_id_ == ROS_TYPE_MESSAGE) {
        const void * item_ptr = member.get_const_function
            ? member.get_const_function(member_data, index)
            : nullptr;
        return cppMessageToVar(item_ptr, cppNestedMembers(member));
    }
    if (member.type_id_ == ROS_TYPE_STRING) {
        std::string value;
        if (member.fetch_function) {
            member.fetch_function(member_data, index, &value);
            return Var(std::move(value));
        }
        const void * item_ptr = member.get_const_function
            ? member.get_const_function(member_data, index)
            : nullptr;
        return item_ptr ? cppStringToVar(member.type_id_, item_ptr) : Var();
    }
    if (member.type_id_ == ROS_TYPE_WSTRING) {
        std::u16string value;
        if (member.fetch_function) {
            member.fetch_function(member_data, index, &value);
            return cppStringToVar(member.type_id_, &value);
        }
        const void * item_ptr = member.get_const_function
            ? member.get_const_function(member_data, index)
            : nullptr;
        return item_ptr ? cppStringToVar(member.type_id_, item_ptr) : Var();
    }

    switch (member.type_id_) {
    case ROS_TYPE_FLOAT:
        return cppArrayPrimitiveToVar<float>(member_data, member, index);
    case ROS_TYPE_DOUBLE:
        return cppArrayPrimitiveToVar<double>(member_data, member, index);
    case ROS_TYPE_LONG_DOUBLE:
        return cppArrayPrimitiveToVar<long double>(member_data, member, index);
    case ROS_TYPE_CHAR:
        return cppArrayPrimitiveToVar<unsigned char>(member_data, member, index);
    case ROS_TYPE_WCHAR:
        return cppArrayPrimitiveToVar<char16_t>(member_data, member, index);
    case ROS_TYPE_BOOLEAN:
        return cppArrayPrimitiveToVar<bool>(member_data, member, index);
    case ROS_TYPE_OCTET:
        return cppArrayPrimitiveToVar<std::byte>(member_data, member, index);
    case ROS_TYPE_UINT8:
        return cppArrayPrimitiveToVar<uint8_t>(member_data, member, index);
    case ROS_TYPE_INT8:
        return cppArrayPrimitiveToVar<int8_t>(member_data, member, index);
    case ROS_TYPE_UINT16:
        return cppArrayPrimitiveToVar<uint16_t>(member_data, member, index);
    case ROS_TYPE_INT16:
        return cppArrayPrimitiveToVar<int16_t>(member_data, member, index);
    case ROS_TYPE_UINT32:
        return cppArrayPrimitiveToVar<uint32_t>(member_data, member, index);
    case ROS_TYPE_INT32:
        return cppArrayPrimitiveToVar<int32_t>(member_data, member, index);
    case ROS_TYPE_UINT64:
        return cppArrayPrimitiveToVar<uint64_t>(member_data, member, index);
    case ROS_TYPE_INT64:
        return cppArrayPrimitiveToVar<int64_t>(member_data, member, index);
    default:
        return Var();
    }
}

struct CppMessageHandle
{
    std::shared_ptr<rcpputils::SharedLibrary> introspection_lib;
    const rosidl_message_type_support_t * introspection_ts = nullptr;
    const CppMessageMembers * introspection_members = nullptr;

    bool ready() const
    {
        return introspection_lib && introspection_ts && introspection_members
            && introspection_members->init_function
            && introspection_members->fini_function;
    }
};

bool loadCppHandle(const std::string& type, CppMessageHandle& handle, std::string& error)
{
    if (type.empty()) {
        error = "type is required";
        return false;
    }

    try {
        handle.introspection_lib = rclcpp::get_typesupport_library(
            type, "rosidl_typesupport_introspection_cpp");
        handle.introspection_ts = rclcpp::get_message_typesupport_handle(
            type, "rosidl_typesupport_introspection_cpp", *handle.introspection_lib);
        handle.introspection_members = static_cast<const CppMessageMembers *>(
            handle.introspection_ts->data);
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }

    if (!handle.ready()) {
        error = "failed to initialize C++ message bridge for type: " + type;
        return false;
    }
    return true;
}

#endif // VE_ROS_HAS_GENERIC_SERVICE

struct MessageHandle
{
    std::shared_ptr<rcpputils::SharedLibrary> message_ts_lib;
    std::shared_ptr<rcpputils::SharedLibrary> introspection_lib;
    std::shared_ptr<rcpputils::SharedLibrary> generator_c_lib;

    const rosidl_message_type_support_t * message_ts = nullptr;
    const rosidl_message_type_support_t * introspection_ts = nullptr;
    const rosidl_typesupport_introspection_c__MessageMembers * introspection_members = nullptr;

    CreateMessageFn create_message = nullptr;
    DestroyMessageFn destroy_message = nullptr;

    bool ready() const
    {
        return message_ts && introspection_ts && introspection_members
            && create_message && destroy_message;
    }
};

bool loadHandle(const std::string& type, MessageHandle& handle, std::string& error)
{
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
        handle.message_ts_lib = rclcpp::get_typesupport_library(type, "rosidl_typesupport_c");
        handle.message_ts = rclcpp::get_message_typesupport_handle(
            type, "rosidl_typesupport_c", *handle.message_ts_lib);

        handle.introspection_lib = rclcpp::get_typesupport_library(type, "rosidl_typesupport_introspection_c");
        handle.introspection_ts = rclcpp::get_message_typesupport_handle(
            type, "rosidl_typesupport_introspection_c", *handle.introspection_lib);
        handle.introspection_members =
            static_cast<const rosidl_typesupport_introspection_c__MessageMembers *>(handle.introspection_ts->data);

        const std::string generator_lib_path =
            rclcpp::get_typesupport_library_path(package_name, "rosidl_generator_c");
        handle.generator_c_lib = std::make_shared<rcpputils::SharedLibrary>(generator_lib_path);

        const std::string symbol_base = normalizeTypeForCSymbol(package_name + "__" + middle_module + "__" + type_name);
        handle.create_message = reinterpret_cast<CreateMessageFn>(
            handle.generator_c_lib->get_symbol(symbol_base + "__create"));
        handle.destroy_message = reinterpret_cast<DestroyMessageFn>(
            handle.generator_c_lib->get_symbol(symbol_base + "__destroy"));
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }

    if (!handle.ready()) {
        error = "failed to initialize message bridge for type: " + type;
        return false;
    }
    return true;
}

bool splitServiceType(const std::string& srv_type,
                     std::string& request_type,
                     std::string& response_type,
                     std::string& error)
{
    // Accept "pkg/SrvName" or "pkg/srv/SrvName"; normalize to pkg/srv/SrvName_{Request,Response}
    const auto first = srv_type.find('/');
    if (first == std::string::npos || first == 0 || first + 1 >= srv_type.size()) {
        error = "service type must be 'pkg/Name' or 'pkg/srv/Name': " + srv_type;
        return false;
    }

    const std::string package = srv_type.substr(0, first);
    const std::string rest = srv_type.substr(first + 1);

    std::string name;
    const auto second = rest.find('/');
    if (second == std::string::npos) {
        name = rest;
    } else {
        const std::string middle = rest.substr(0, second);
        if (middle != "srv") {
            error = "service type middle segment must be 'srv': " + srv_type;
            return false;
        }
        name = rest.substr(second + 1);
    }

    if (package.empty() || name.empty()) {
        error = "service type has empty package or name: " + srv_type;
        return false;
    }

    request_type = package + "/srv/" + name + "_Request";
    response_type = package + "/srv/" + name + "_Response";
    return true;
}

bool doDeserialize(const MessageHandle& handle,
                   const rclcpp::SerializedMessage& message,
                   Var& out,
                   std::string& error)
{
    if (!handle.ready()) {
        error = "bridge handle is not initialized";
        return false;
    }

    void * ros_message = handle.create_message();
    if (!ros_message) {
        error = "failed to allocate message instance";
        return false;
    }

    const auto & serialized = message.get_rcl_serialized_message();
    rmw_serialized_message_t rmw_message = serialized;

    const rmw_ret_t rc = rmw_deserialize(&rmw_message, handle.message_ts, ros_message);
    if (rc != RMW_RET_OK) {
        handle.destroy_message(ros_message);
        error = "rmw_deserialize failed";
        return false;
    }

    out = messageToVar(ros_message, handle.introspection_members);
    handle.destroy_message(ros_message);
    return true;
}

bool doSerialize(const MessageHandle& handle,
                 const Var& value,
                 rclcpp::SerializedMessage& out,
                 std::string& error)
{
    if (!handle.ready()) {
        error = "bridge handle is not initialized";
        return false;
    }

    void * ros_message = handle.create_message();
    if (!ros_message) {
        error = "failed to allocate message instance";
        return false;
    }

    if (!varToMessage(value, ros_message, handle.introspection_members, error)) {
        handle.destroy_message(ros_message);
        return false;
    }

    auto & rmw_message = out.get_rcl_serialized_message();
    const rmw_ret_t rc = rmw_serialize(ros_message, handle.message_ts, &rmw_message);
    handle.destroy_message(ros_message);
    if (rc != RMW_RET_OK) {
        error = "rmw_serialize failed";
        return false;
    }
    return true;
}

} // namespace

struct DynamicTypesupportBridge::Private
{
    MessageHandle message;
#ifdef VE_ROS_HAS_GENERIC_SERVICE
    CppMessageHandle request;
    CppMessageHandle response;
#endif
};

DynamicTypesupportBridge::DynamicTypesupportBridge(const std::string& type, std::string& error)
{
    initialize(type, error);
}

bool DynamicTypesupportBridge::initialize(const std::string& type, std::string& error)
{
    ready_ = false;
    is_service_ = false;
    type_ = type;
    p_ = std::make_shared<Private>();

    if (!loadHandle(type, p_->message, error))
        return false;

    ready_ = true;
    return true;
}

bool DynamicTypesupportBridge::initializeService(const std::string& srv_type, std::string& error)
{
    ready_ = false;
    is_service_ = false;
    type_ = srv_type;
    p_ = std::make_shared<Private>();

#ifndef VE_ROS_HAS_GENERIC_SERVICE
    error = "GenericClient is unavailable; service calls require Jazzy (rclcpp 28+)";
    return false;
#else
    std::string request_type;
    std::string response_type;
    if (!splitServiceType(srv_type, request_type, response_type, error))
        return false;

    if (!loadCppHandle(request_type, p_->request, error))
        return false;
    if (!loadCppHandle(response_type, p_->response, error))
        return false;

    ready_ = true;
    is_service_ = true;
    return true;
#endif
}

bool DynamicTypesupportBridge::deserializeToVar(const rclcpp::SerializedMessage& message,
                                                ve::Var& out,
                                                std::string& error) const
{
    if (!ready_ || !p_) {
        error = "bridge is not initialized";
        return false;
    }
    if (is_service_) {
        error = "serialized message APIs are not available in service mode";
        return false;
    }
    return doDeserialize(p_->message, message, out, error);
}

bool DynamicTypesupportBridge::serializeFromVar(const ve::Var& value,
                                                rclcpp::SerializedMessage& out,
                                                std::string& error) const
{
    if (!ready_ || !p_) {
        error = "bridge is not initialized";
        return false;
    }
    if (is_service_) {
        error = "serialized message APIs are not available in service mode";
        return false;
    }
    return doSerialize(p_->message, value, out, error);
}

std::shared_ptr<void> DynamicTypesupportBridge::requestFromVar(const ve::Var& value,
                                                               std::string& error) const
{
#ifndef VE_ROS_HAS_GENERIC_SERVICE
    (void)value;
    error = "GenericClient is unavailable; service calls require Jazzy (rclcpp 28+)";
    return {};
#else
    if (!ready_ || !p_ || !is_service_) {
        error = "bridge was not initialized as a service";
        return {};
    }

    const auto * members = p_->request.introspection_members;
    void * request = nullptr;
    try {
        request = ::operator new(members->size_of_);
        members->init_function(
            request, rosidl_runtime_cpp::MessageInitialization::ALL);
    } catch (const std::exception& e) {
        ::operator delete(request);
        error = std::string("failed to initialize C++ service request: ") + e.what();
        return {};
    } catch (...) {
        ::operator delete(request);
        error = "failed to initialize C++ service request";
        return {};
    }

    const auto owner = p_;
    std::shared_ptr<void> request_owner(request, [owner](void* message) {
        owner->request.introspection_members->fini_function(message);
        ::operator delete(message);
    });

    try {
        if (!cppVarToMessage(value, request, members, error))
            return {};
    } catch (const std::exception& e) {
        error = std::string("failed to populate C++ service request: ") + e.what();
        return {};
    } catch (...) {
        error = "failed to populate C++ service request";
        return {};
    }
    return request_owner;
#endif
}

bool DynamicTypesupportBridge::responseToVar(const void* response,
                                             ve::Var& out,
                                             std::string& error) const
{
#ifndef VE_ROS_HAS_GENERIC_SERVICE
    (void)response;
    (void)out;
    error = "GenericClient is unavailable; service calls require Jazzy (rclcpp 28+)";
    return false;
#else
    if (!ready_ || !p_ || !is_service_) {
        error = "bridge was not initialized as a service";
        return false;
    }
    if (!response || !p_->response.ready()) {
        error = "service response is unavailable";
        return false;
    }
    try {
        out = cppMessageToVar(response, p_->response.introspection_members);
    } catch (const std::exception& e) {
        error = std::string("failed to read C++ service response: ") + e.what();
        return false;
    } catch (...) {
        error = "failed to read C++ service response";
        return false;
    }
    return true;
#endif
}

} // namespace ve::ros::rclcpp_backend
