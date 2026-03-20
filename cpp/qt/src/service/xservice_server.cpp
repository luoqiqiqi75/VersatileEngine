#include "xservice_server.h"

#include "asio2/asio2.hpp"

#include "ve/qt/imol_legacy.h"

//VE_REGISTER_MODULE(XServiceServer, ve::service::XServiceServer)

namespace ve::service {

struct XServiceServer::Private
{
public:
    imol::ModuleObject* d = nullptr;
};

XServiceServer::XServiceServer() : Module("ve.qt.xservice.server"), _p(new Private) {
    _p->d = imol::legacy::d(QStringLiteral("ve.server.xservice"));
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
