#ifndef IMOL_STATEMANAGER_H
#define IMOL_STATEMANAGER_H

#include "core_global.h"

#include <QObject>
#include <QHash>
#include <QVariant>

namespace imol {
class CORESHARED_EXPORT StateAction : public QObject
{
    Q_OBJECT

public:

};

class CORESHARED_EXPORT StateObject : public QObject
{
    Q_OBJECT

public:
    explicit StateObject(const QString &name, StateObject *previous_state = nullptr, StateObject *next_state = nullptr, QObject *parent = nullptr);

    virtual bool isEmptyState() {return false;}

    QString name() const;

    StateObject *previousState() const;
    virtual void setPreviousState(StateObject *prevous_state, bool is_bidirectional = true);

    StateObject *nextState() const;
    virtual void setNextState(StateObject *next_state, bool is_bidirectional = true);

    QVariant var() const;
    virtual void setVar(const QVariant &var);

signals:
    void rein(imol::StateObject *from); //deutsch
    void raus(imol::StateObject *to); //deutsch

public slots:

private:
    QString m_name;

    StateObject *m_previous;
    StateObject *m_next;

    QVariant m_var;
};

class CORESHARED_EXPORT StateWatcher : public QObject
{
    Q_OBJECT

public:
    explicit StateWatcher(QObject *parent = nullptr);

signals:
    void changed(imol::StateObject *from, imol::StateObject *to);
};

class CORESHARED_EXPORT EmptyStateObject : public StateObject
{
    Q_OBJECT

public:
    explicit EmptyStateObject(QObject *parent = nullptr) : StateObject("@e", nullptr, nullptr, parent) {}

    bool isEmptyState() override {return true;}

    void setPreviousState(StateObject *, bool) override {}
    void setNextState(StateObject *, bool) override {}
    void setVar(const QVariant &) override {}
};

class CORESHARED_EXPORT StateManager : public QObject
{
    Q_OBJECT

public:
    enum ConnectionType {
        NO_CONNECTION,
        SEQUENTIAL_CONNECTION,
        LOOP_CONNECTION,
        CROSS_CONNECTION
    };

    explicit StateManager(QObject *parent = nullptr);
    static StateManager & instance();

    EmptyStateObject * emptyState() const;
    StateWatcher * emptyWatcher() const;

    StateObject * regist(QObject *context, const QString &state_name, bool is_current = false);
    bool regist(QObject *context, const QStringList &state_names, bool first_is_current = true, ConnectionType connection_type = SEQUENTIAL_CONNECTION);
    bool cancel(QObject *context, const QString &state_name = "");

    StateObject *get(QObject *context, const QString &state_name) const;

    StateObject *current(QObject *context) const;

    void change(QObject *context, StateObject *state_obj);
    void change(QObject *context, const QString &state_name);
    void toNext(QObject *context);
    void toPrevious(QObject *context);

    StateWatcher *watcher(QObject *context) const;

signals:

public slots:

private:
    EmptyStateObject *m_empty_state;
    StateWatcher *m_empty_watcher;

    QHash<QObject *, QHash<QString, StateObject *> *> m_states;
    QHash<QObject *, StateObject *> m_cur_state_hash;
    QHash<QObject *, StateWatcher *> m_watcher_hash;
};
}
//output methods
CORESHARED_EXPORT imol::StateManager & state();

#endif // IMOL_STATEMANAGER_H
