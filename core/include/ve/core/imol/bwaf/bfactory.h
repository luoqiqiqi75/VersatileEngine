#ifndef IMOL_BFACTORY_H
#define IMOL_BFACTORY_H

#include "core/creatormanager.h"

#include "bwaf_global.h"

namespace bwaf {
class BModule;
typedef IMOL_CREATOR_TYPE(BModule *, (QObject *)) BFactory;
}

BWAFSHARED_EXPORT bwaf::BFactory & bfactory();

#define IMOL_API 11

namespace imol {
typedef IMOL_CREATOR_TYPE(int, ()) PluginManager;
BWAFSHARED_EXPORT PluginManager & pluginManager();
}

#define IMOL_PLUGIN(KEY, CLASS_NAME) IMOL_AUTO_RUN(bfactory().regist(KEY, [] (QObject *parent) -> bwaf::BModule * {return new CLASS_NAME(parent);}); \
    imol::pluginManager().regist(KEY, [] () -> int { return IMOL_API; }))

#define BFACTORY_REGIST_MODULE(KEY, CLASS_NAME) IMOL_AUTO_RUN(bfactory().regist(KEY, [] (QObject *parent) -> bwaf::BModule * {return new CLASS_NAME(parent);}))

#define BFACTORY_CREATE_SUBMODULES(PARENT, PREFIX) \
    foreach (QString creator_key, bfactory().keyMobj(PREFIX)->cmobjNames()) { \
        bwaf::BModule *bmodule = bfactory().create(mName(PREFIX, creator_key), PARENT); \
        if (bmodule) PARENT->addSubmodule(bmodule); \
    }

//specific registration
#define BFACTORY_REGIST_SIMPLE_WIDGET_MODULE(KEY, WIDGET_CLASS_NAME) \
    class WIDGET_CLASS_NAME##BModule : public bwaf::BModule \
    { \
    public: \
        WIDGET_CLASS_NAME##BModule(QObject *parent = nullptr) : bwaf::BModule(parent), m_wgt(new WIDGET_CLASS_NAME(nullptr)) {} \
        ~WIDGET_CLASS_NAME##BModule() {delete m_wgt;} \
        QString name() const override {return KEY;} \
        QWidget * wgt() override {return m_wgt;} \
    private: \
        QWidget *m_wgt; \
    }; \
    IMOL_AUTO_RUN(bfactory().regist(KEY, [] (QObject *parent) -> bwaf::BModule * {return new WIDGET_CLASS_NAME##BModule(parent);}))

#endif // IMOL_BFACTORY_H
