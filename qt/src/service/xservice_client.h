#ifndef IMOL_XSERVICE_H
#define IMOL_XSERVICE_H

#include "imol/modulemanager.h"
#include "ve/core/module.h"

namespace ix {
using Node = imol::ModuleObject;
Node* createNodeAt(Node* node, const QString& path);
}

class VE_API XService : public ve::Module
{
public:
    enum Error : int {
        Ok              = 0,
        Duplicate       = -0x00010000,
        Timeout         = -0x00010001
    };

public:
    explicit XService(QObject *parent = nullptr);
    virtual ~XService();

    ix::Node* mobj() const; // support

    ix::Node * dataNode(const QString &key = "");
    ix::Node * commandNode(const QString &key = "");

public: // one key utils
    ix::Node * get(QObject *context, const QString &key);
    ix::Node * get(QObject *context, const QString &key, const QString &sub_key);
    ix::Node * set(QObject *context, const QString &key, ix::Node *param = nullptr);
    ix::Node * set(QObject *context, const QString &key, const QString &sub_key, ix::Node *param = nullptr);
    ix::Node * set(QObject *context, const QString &key, const QString &sub_key, const QVariant &var);
    ix::Node * watch(QObject *context, const QString &key, bool watching = true);

    ix::Node * command(QObject *context, const QString &key, ix::Node *param = nullptr);
    ix::Node * command(QObject *context, const QString &key, const QVariant &var);
    ix::Node * execCommand(QObject *context, const QString &key, ix::Node *param = nullptr);
    ix::Node * execCommand(QObject *context, const QString &key, const QVariant &var);

    ix::Node * execImol(QObject *context, const QString &key, ix::Node *param = nullptr);

public: // ref node utils
    ix::Node * getAt(QObject *context, ix::Node *ref, int search_level = 2);
    ix::Node * getAt(QObject *context, const QString &key, ix::Node *ref, int search_level = 0);
    ix::Node * getAt(QObject *context, const QString &key, const QString &sub_key, ix::Node *ref);

    ix::Node * setAt(QObject *context, const QString &key, ix::Node *ref, int search_level = 0);
    ix::Node * setAt(QObject *context, const QString &key, const QString &sub_key, ix::Node *ref);

    ix::Node * execCommandAt(QObject *context, const QString &key, ix::Node *ref);

//signals:
//    void support(ix::Node *ref, const ix::Node *pkg);

private:
    void init();
    void idle();
    void detach();

//private slots:
    void onConnected();
    void onDisconnected();
    void onReceived(const QByteArray &bytes);

private:
    VE_DECLARE_PRIVATE
};

VE_API XService * xservice();

#endif // IMOL_XSERVICE_H
