#include "ve/core/node.h"

namespace {
struct Marker {
    Marker() { ve::n("test/plugin_loaded")->set(true); }
    ~Marker() { ve::n("test/plugin_loaded")->set(false); }
} marker;
}
