#ifndef IMOL_CREATORMANAGER_H
#define IMOL_CREATORMANAGER_H

#include <functional>

#include <QHash>

#ifndef IMOL_ROOT_MODULE_NAME
#include "modulemanager.h"
#endif

namespace imol {
class ModuleObject;
template <typename Ret, class Functor>
class CreatorManager
{
public:
    explicit CreatorManager() : m_key_mobj(new imol::ModuleObject("_creator_key")) {}
    ~CreatorManager() {delete m_key_mobj;}
    static CreatorManager & instance()
    {
        static CreatorManager manager;
        return manager;
    }

    imol::ModuleObject * keyMobj(const QString &rname = "") const {return rname.isEmpty() ? m_key_mobj : m_key_mobj->r(rname);}

    void regist(const QString &full_key, std::function<Functor> func)
    {
        if (m_funcs.contains(full_key)) return;
        m_funcs.insert(full_key, func);
        m_key_mobj->set(m_key_mobj, full_key, QVariant());
    }
    void cancel(const QString &full_key)
    {
        if (!m_funcs.contains(full_key)) return;
        m_key_mobj->remove(m_key_mobj, full_key);
        m_funcs.remove(full_key);
    }

    template <typename... Params>
    Ret create(const QString &full_key, Params... params) {return m_funcs.contains(full_key) ? m_funcs.value(full_key)(params...) : (Ret)(NULL);}
    Ret create(const QString &full_key) {return m_funcs.contains(full_key) ? m_funcs.value(full_key)() : (Ret)(NULL);}

private:
    imol::ModuleObject *m_key_mobj;
    QHash<QString, std::function<Functor>> m_funcs;
};
}

//output macro
#define IMOL_CREATOR(_FUNC_OUT, _FUNC_IN) IMOL_CREATOR_TYPE(_FUNC_OUT, _FUNC_IN)::instance()
#define IMOL_CREATOR_TYPE(_FUNC_OUT, _FUNC_IN) imol::CreatorManager<_FUNC_OUT, _FUNC_OUT _FUNC_IN>

#define IMOL_AUTO_RUN_NAME(_PREFIX, _SUFFIX) IMOL_AUTO_RUN_NAME_I(_PREFIX, _SUFFIX)
#define IMOL_AUTO_RUN_NAME_I(_PREFIX, _SUFFIX) _PREFIX##_SUFFIX
#define IMOL_AUTO_RUN_FUNC IMOL_AUTO_RUN_NAME(_imol_auto_run_func_, __LINE__)
#define IMOL_AUTO_RUN_VAR IMOL_AUTO_RUN_NAME(_imol_auto_run_var_, __LINE__)
#define IMOL_AUTO_RUN(_CONTENT) namespace {int IMOL_AUTO_RUN_FUNC() {_CONTENT; return 0;} int IMOL_AUTO_RUN_VAR = IMOL_AUTO_RUN_FUNC();}

#endif // IMOL_CREATORMANAGER_H
