#pragma once

#include "ve/core/var.h"

namespace ve::ros {

VE_API Var::ListV listTopics(const std::string& filter = "");
VE_API Var::DictV topicInfo(const std::string& topic_name);

} // namespace ve::ros
