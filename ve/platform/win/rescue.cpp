#include "ve/service/rescue.h"

#include "StackWalker/interface.h"

namespace ve {
namespace service {

void setupRescue()
{
    set_default_handler();
}

} // namespace service
} // namespace ve
