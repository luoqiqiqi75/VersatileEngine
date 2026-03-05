#include "ve/ros/core/common.h"

namespace hemera::util {

bool startsWith(const std::string& str, const std::string& sub_str, bool ignore_case)
{
    return sub_str.empty() || str.find(sub_str) == 0;
}

}
