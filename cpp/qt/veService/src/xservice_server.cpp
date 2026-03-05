#include "xservice_server.h"

#include "asio2/asio2.hpp"

//VE_REGISTER_MODULE(XServiceServer, ve::service::XServiceServer)

namespace ve::service {

struct XServiceServer::Private
{
public:
    Data* d;
};

XServiceServer::XServiceServer() : _p(new Private)
{
    _p->d = ve::d("ve.server.xservice");


}

XServiceServer::~XServiceServer()
{

}

void XServiceServer::init()
{

}

void XServiceServer::ready()
{

}

void XServiceServer::deinit()
{

}

}
