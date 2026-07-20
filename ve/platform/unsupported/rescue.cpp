#include "ve/service/rescue.h"

namespace ve {
namespace service {

void setupRescue()
{
    (void)trySetupRescue();
}

bool trySetupRescue() noexcept
{
    return false;
}

} // namespace service
} // namespace ve
