// service.h — ve::dds::Service<ReqPST, RepPST>
//
// Header-only typed DDS service with optional command binding.
//
// Usage (IDL approach):
//   Service<GetDataReqPST, GetDataRepPST> srv(participant, "get_data",
//       [](const GetDataReq& req, GetDataRep& rep) -> int {
//           rep.value(42);
//           return 0;
//       });
//
// Command binding:
//   srv.bindCommand("get_data");
//   // DDS request → command::call("get_data", Var(req_fields...))

#pragma once

#include "participant.h"
#include "ve/core/command.h"

namespace ve::dds {

template<typename ReqPST, typename RepPST>
class Service
{
public:
    using ReqT    = typename ReqPST::type;
    using RepT    = typename RepPST::type;
    using Handler = std::function<int(const ReqT&, RepT&)>;

    Service(Participant& p, const std::string& name, Handler handler)
        : _participant(p), _name(name), _handler(std::move(handler))
    {
        p.createService<ReqPST, RepPST>(_name,
            [this](const ReqT& req, RepT& rep) -> int {
                if (_handler) return _handler(req, rep);
                return -1;
            });
    }

    // Bind a command key: incoming DDS service request will invoke
    // command::call(cmd_key, input) where input is built by reqToVar.
    // The result is converted back via varToRep.
    void bindCommand(const std::string& cmd_key,
                     std::function<Var(const ReqT&)>  reqToVar,
                     std::function<void(const Result&, RepT&)> resultToRep)
    {
        _handler = [cmd_key, reqToVar, resultToRep](const ReqT& req, RepT& rep) -> int {
            Var input = reqToVar ? reqToVar(req) : Var();
            Result r = command::call(cmd_key, input);
            if (resultToRep) resultToRep(r, rep);
            return r.isSuccess() ? 0 : r.code();
        };
    }

private:
    Participant& _participant;
    std::string  _name;
    Handler      _handler;
};

} // namespace ve::dds
