#include "ve/qt/widget/terminal.h"

#include "terminal_widget.h"
#include "server.h"

namespace ve {

namespace terminal {

QWidget* widget()
{
    return &Terminal::instance();
}

void startServer(int port, QObject* parent)
{
    auto server = new Server(parent);
    server->start(port);
}

}

}

