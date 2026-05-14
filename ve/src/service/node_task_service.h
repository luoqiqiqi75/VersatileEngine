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

    // The Pipeline (`detached`) owns its context; deleting the pipeline cleans
    // up both. Task service takes ownership of `detached`.
    std::string attach(const std::string& cmdKey, const Var& id,
                       Pipeline* detached, DoneFn onDone);

private:
    VE_DECLARE_UNIQUE_PRIVATE
};

} // namespace service
} // namespace ve
