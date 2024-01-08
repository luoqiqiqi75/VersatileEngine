#include "bwaf/bmodule.h"

#include <veCommon>

using namespace bwaf;
using namespace imol;

imol::ModuleObject g_bmodule_mobj("g");

BModule::BModule(QObject *parent) : QObject(parent),
    m_state(STATE_BORN),
    m_mobj(m().emptyMobj())
{
    m_submodules.clear();
}

BModule::~BModule()
{
    qDeleteAll(m_submodules);
}

BModule * BModule::parentModule() const
{
    return qobject_cast<BModule *>(parent());
}

QWidget * BModule::wgt()
{
    return nullptr;
}

void BModule::bindMobj(const QString &full_name)
{
    m().regist(this, full_name);
    m_mobj = m(full_name);

    if (bModule(full_name)) {
        ELOG << "<bwaf>" << full_name << " already bounded";
        return;
    }

    g_bmodule_mobj.set(this, full_name, QVariant::fromValue<BModule *>(this));
    connect(this, &BModule::destroyed, &g_bmodule_mobj, [=] {g_bmodule_mobj.remove(this, full_name);});
}

void BModule::unbindMobj()
{
    g_bmodule_mobj.remove(this, m_mobj->fullName());
    m_mobj = m().emptyMobj();
}

void BModule::bindMobjJson(const QString &full_name, const QString &json_path, bool auto_watch)
{
    bindMobj(full_name);

    m_mobj->watch(auto_watch);
    m_mobj->importFromJson(this, m().readFromJson(json_path));
}

imol::ModuleObject * BModule::mobj() const
{
    return m_mobj;
}

QList<BModule *> BModule::submodules() const
{
    return m_submodules;
}

QStringList BModule::submoduleNames() const
{
    QStringList submodule_names;
    foreach (BModule *module, m_submodules) {
        submodule_names.append(module->name());
    }
    return submodule_names;
}

void BModule::insertSubmodule(BModule *module, int index)
{
    m_submodules.insert(index, module);
}

void BModule::addSubmodule(BModule *module)
{
    m_submodules.append(module);
}

void BModule::addSubmodules(const QList<BModule *> &modules)
{
    m_submodules.append(modules);
}

BModule * BModule::findSubmodule(const QString &name) const
{
    foreach (BModule *submodule, m_submodules) {
        if (name == submodule->name()) return submodule;
    }
    return nullptr;
}

void BModule::removeSubmodule(BModule *module)
{
    m_submodules.removeOne(module);
}

BModule::ModuleState BModule::moduleState() const
{
    return m_state;
}

void BModule::startSetup()
{
#ifndef QT_NO_DEBUG
    emit moduleStateAboutToChange(STATE_SETUP);
#endif
    //setup this module before submodules
    this->setup();
    emit moduleStateChanged(m_state = STATE_SETUP);

    foreach (BModule *submodule, m_submodules) {
        submodule->startSetup();
    }
    this->afterSetup();
}

void BModule::startInit()
{
#ifndef QT_NO_DEBUG
    emit moduleStateAboutToChange(STATE_INIT);
#endif
    //init this module before submodules
    this->init();
    emit moduleStateChanged(m_state = STATE_INIT);

    foreach (BModule *submodule, m_submodules) {
        submodule->startInit();
    }
    this->afterInit();
}

void BModule::startRender()
{
#ifndef QT_NO_DEBUG
    emit moduleStateAboutToChange(STATE_RENDER);
#endif
    //render this module before submodules
    this->render();
    emit moduleStateChanged(m_state = STATE_RENDER);

    foreach (BModule *submodule, m_submodules) {
        submodule->startRender();
    }
    this->afterRender();
}

void BModule::startIdle()
{
#ifndef QT_NO_DEBUG
    emit moduleStateAboutToChange(STATE_IDLE);
#endif
    //idle this module before submodules
    this->idle();
    emit moduleStateChanged(m_state = STATE_IDLE);

    foreach (BModule *submodule, m_submodules) {
        submodule->startIdle();
    }
    this->afterIdle();
}

void BModule::startDetach()
{
#ifndef QT_NO_DEBUG
    emit moduleStateAboutToChange(STATE_DETACH);
#endif
    //destroy this module after submodules
    this->beforeDetach();
    foreach (BModule *submodule, m_submodules) {
        submodule->startDetach();
    }

    this->detach();

    //clear data
//    if (!m_mobj->isEmptyMobj()) {
//        if (!m_mobj->pmobj()->isEmptyMobj()) {
//            m_mobj->pmobj()->remove(this, m_mobj);
//        } else {
//            m().cancel(this, m_mobj->fullName());
//            delete m_mobj;
//        }
//        m_mobj = m().emptyMobj();
//    }

    emit moduleStateChanged(m_state = STATE_DETACH);
}

void BModule::runToNextState()
{
    switch (m_state) {
    case STATE_BORN: startSetup(); break;
    case STATE_SETUP: startInit(); break;
    case STATE_INIT: startRender(); break;
    case STATE_RENDER: startIdle(); break;
    default: break;
    }
}

void BModule::runToState(ModuleState target_state)
{
    while (m_state < STATE_IDLE && m_state < target_state) {
        runToNextState();
    }
}

void BModule::setup() {}
void BModule::afterSetup() {}

void BModule::init() {}
void BModule::afterInit() {}

void BModule::render() {}
void BModule::afterRender() {}

void BModule::idle() {}
void BModule::afterIdle() {}

void BModule::detach() {}
void BModule::beforeDetach() {}

BModule * bModule(const QString &full_name)
{
    return g_bmodule_mobj.r(full_name)->get().value<BModule *>();
}
