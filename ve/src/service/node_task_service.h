#pragma once

#include "ve/global.h"
#include "ve/core/var.h"

#include <functional>
#include <string>

namespace ve {

class Node;
class Pipeline;

namespace service {

class VE_API NodeTaskService
{
public:
    using DoneFn = std::function<void(const Node&)>;

    explicit NodeTaskService(Node* root);
    ~NodeTaskService();

    std::string attach(const std::string& cmdKey, const Var& id,
                       Node* cmdCtx, Pipeline* detached, DoneFn onDone);

private:
    VE_DECLARE_UNIQUE_PRIVATE
};

} // namespace service
} // namespace ve
