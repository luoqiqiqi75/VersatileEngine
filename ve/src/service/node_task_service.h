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

    // The Pipeline owns its context. Task service observes completion and
    // records the result; lifecycle is owned by the caller or ve::pipeline::start.
    std::string attach(const std::string& cmdKey, const Var& id,
                       Pipeline* pipeline, DoneFn onDone);

private:
    VE_DECLARE_UNIQUE_PRIVATE
};

} // namespace service
} // namespace ve
