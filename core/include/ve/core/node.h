// ----------------------------------------------------------------------------
// node.h
// ----------------------------------------------------------------------------
// Copyright (c) 2023-present Thilo and VersatileEngine contributors.
// Licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
// See LICENSE file in the project root for full license information.
// ----------------------------------------------------------------------------

#pragma once

#include "base.h"

namespace ve {

struct Structure
{

};

struct VE_API Node
{

public:
    Node();
    ~Node();



    Node* parent();
    Node* parent(int level);



private:
    VE_DECLARE_UNIQUE_PRIVATE
};


// todo
struct Data {

};

}
