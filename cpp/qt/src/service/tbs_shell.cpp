// tbs_shell.cpp — imol terminal hooks for ve::TcpBinClient (TBS transport)
#include "ve/core/module.h"
#include "ve/core/var.h"
#include "ve/core/impl/json.h"
#include "ve/service/tcp_bin_client.h"

#include "imol/commandmanager.h"

#include <QObject>
#include <QStringList>

#include <memory>

namespace ve::service {

namespace shell_internal {

std::unique_ptr<TcpBinClient> g_client;

class TbsClientShellCommand : public imol::BaseCommand
{
public:
    explicit TbsClientShellCommand(QObject* parent = nullptr)
        : imol::BaseCommand(QStringLiteral("tbs.client"), false, parent)
    {
    }

    QString usage() const override
    {
        return tr("tbs.client connect [host] [port] | disconnect | call <op> <path>");
    }

protected:
    void run(imol::ModuleObject*, const QString& param) override
    {
        const QString t = param.trimmed();
        if (t.startsWith(QStringLiteral("connect"))) {
            QStringList parts = t.split(QLatin1Char(' '), Qt::SkipEmptyParts);
            std::string host = "127.0.0.1";
            uint16_t port    = 5065;
            if (parts.size() >= 2) {
                host = parts[1].toStdString();
            }
            if (parts.size() >= 3) {
                bool ok = false;
                int p   = parts[2].toInt(&ok);
                if (ok && p > 0 && p < 65536) {
                    port = static_cast<uint16_t>(p);
                }
            }
            if (!g_client) {
                g_client = std::make_unique<TcpBinClient>();
            }
            const bool ok = g_client->connect(host, port);
            output(ok ? QStringLiteral("<tbs.client> connected")
                      : QStringLiteral("<tbs.client> connect failed"));
            return;
        }
        if (t.startsWith(QStringLiteral("disconnect"))) {
            if (g_client) {
                g_client->disconnect();
            }
            output(QStringLiteral("<tbs.client> disconnected"));
            return;
        }
        if (t.startsWith(QStringLiteral("call"))) {
            if (!g_client || !g_client->isConnected()) {
                error(QStringLiteral("tbs.client not connected"));
                return;
            }
            const QString rest = t.mid(4).trimmed();
            const int sp       = rest.indexOf(QLatin1Char(' '));
            if (sp <= 0) {
                error(QStringLiteral("usage: tbs.client call <op> <path>"));
                return;
            }
            const QString op   = rest.left(sp).trimmed();
            const QString path = rest.mid(sp + 1).trimmed();
            if (op.isEmpty() || path.isEmpty()) {
                error(QStringLiteral("usage: tbs.client call <op> <path>"));
                return;
            }
            Var::DictV dict;
            dict["op"]   = Var(op.toStdString());
            dict["path"] = Var(path.toStdString());
            dict["args"] = Var();
            Var req(std::move(dict));
            Var resp;
            if (!g_client->call(std::move(req), resp, 30000)) {
                error(QStringLiteral("tbs.client call failed or timed out"));
                return;
            }
            output(QString::fromStdString(json::stringify(resp)));
            return;
        }
        error(QStringLiteral("unknown tbs.client subcommand"));
    }
};

} // namespace shell_internal

class TbsShellModule : public Module
{
public:
    explicit TbsShellModule(const std::string& name)
        : Module(name)
    {
    }

protected:
    void init() override { imol::command().regist(new shell_internal::TbsClientShellCommand); }
};

} // namespace ve::service

VE_REGISTER_MODULE(ve.service.tbs, ve::service::TbsShellModule)
