#ifndef IMOL_BMODULE_H
#define IMOL_BMODULE_H

#include "bwaf_global.h"

#include <QObject>

namespace imol { class ModuleObject; }
namespace bwaf {
class BWAFSHARED_EXPORT BModule : public QObject
{
    Q_OBJECT

public:
    enum ModuleState {
        STATE_BORN,
        STATE_SETUP,
        STATE_INIT,
        STATE_RENDER,
        STATE_IDLE,
        STATE_DETACH,
        STATE_COUNT
    };
    //!
    //! The constructor achieves the registration of a module
    //!
    explicit BModule(QObject *parent = nullptr);
    virtual ~BModule();

    BModule * parentModule() const;
    //!
    //! \brief name is a necessery property for a module
    //!
    virtual QString name() const = 0;
    //!
    //! \brief wgt is an optional property for the module contains a widget
    //!
    virtual QWidget * wgt();

    void bindMobj(const QString &full_name);
    void unbindMobj();

    void bindMobjJson(const QString &full_name, const QString &json_path, bool auto_watch = true);

    imol::ModuleObject * mobj() const;

    QList<BModule *> submodules() const;
    QStringList submoduleNames() const;
    void insertSubmodule(BModule *module, int index);
    void addSubmodule(BModule *module);
    void addSubmodules(const QList<BModule *> &modules);
    BModule * findSubmodule(const QString &name) const;
    void removeSubmodule(BModule *module);

    ModuleState moduleState() const;

signals:
    //! signals for state changing only availabe if NO QT_NO_DEBUG
    void moduleStateAboutToChange(bwaf::BModule::ModuleState module_state);
    void moduleStateChanged(bwaf::BModule::ModuleState module_state);

public slots:
    void startSetup();
    void startInit();
    void startRender();
    void startIdle();
    void startDetach();

    void runToNextState();
    void runToState(bwaf::BModule::ModuleState target_state);

private:
    //!
    //! \brief setup procedure is responsible for creating \a submodules
    //!
    virtual void setup();
    //! normally parent module \fn setup before submodules \fn setup, in case of still remaining work,
    //! override this function.
    virtual void afterSetup();
    //!
    //! \brief init procedure comes after all submodules are created, now you can establish the connections,
    //! and create controls releated to \a resource
    //!
    virtual void init();
    //! normally parent module \fn init before submodules \fn init, in case of still remaining work,
    //! override this function.
    virtual void afterInit();
    //!
    //! \brief render procedure redraws the widget, creating item is forbidden during this part
    //!
    virtual void render();
    //! normally parent module \fn render before submodules \fn render, in case of still remaining work,
    //! override this function.
    virtual void afterRender();
    //!
    //! \brief idle procedure starts when the module is ready
    //!
    virtual void idle();
    //! normally parent module \fn idle before submodules \fn idle, in case of still remaining work,
    //! override this function.
    virtual void afterIdle();
    //!
    //! \brief detach procedure removes everything related to external modules
    //!
    virtual void detach();
    //! normally parent module \fn detach after submodules \fn detach, in case of ealier work,
    //! override this function.
    virtual void beforeDetach();

protected:
    QList<BModule *> m_submodules;

private:
    ModuleState m_state;
    imol::ModuleObject *m_mobj;
};
}

BWAFSHARED_EXPORT bwaf::BModule * bModule(const QString &full_name);

#endif // IMOL_BMODULE_H
