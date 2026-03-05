///////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2023
//////////////////////////////////////////////////////////////////////////////

#pragma once

#include "ve/ros/core/common.h"

namespace hemera::service::standard {

enum SerializeType {
    SERIALIZE_STRING,
    SERIALIZE_YAML,
    SERIALIZE_JSON
};

template<SerializeType ST = SERIALIZE_YAML> std::string dataList(const std::string &str);
template<SerializeType ST = SERIALIZE_YAML> std::string dataGet(const std::string &str);
template<SerializeType ST = SERIALIZE_YAML> std::string dataSet(const std::string &str);

}
