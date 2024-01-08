#include "bwaf/bfactory.h"

#include <veCommon>

VE_REGISTER_VERSION("imol.bwaf.bfactory", 1)

bwaf::BFactory & bfactory()
{
    static bwaf::BFactory mgr;
    return mgr;
}

namespace imol {

PluginManager & pluginManager()
{
    static PluginManager mgr;
    return mgr;
}

}
