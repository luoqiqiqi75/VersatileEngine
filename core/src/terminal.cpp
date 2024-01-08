#include "ve/core/terminal.h"

#include "imol/program/terminal.h"

namespace ve {

namespace terminal {

QWidget* widget()
{
    return &Terminal::instance();
}

}

}

