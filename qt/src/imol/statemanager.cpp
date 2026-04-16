#include "imol/statemanager.h"

using namespace imol;

//state object class
StateObject::StateObject(const QString &name, StateObject *previous_state, StateObject *next_state, QObject *parent) : QObject(parent),
    m_name(name),
    m_previous(previous_state),
    m_next(next_state)
{

}

QString StateObject::name() const
{
    return m_name;
}

StateObject * StateObject::previousState() const
{
    return m_previous ? m_previous : state().emptyState();
}

void StateObject::setPreviousState(StateObject *prevous_state, bool is_bidirectional)
{
    m_previous = prevous_state;

    if (is_bidirectional && prevous_state) prevous_state->setNextState(this, false);
}

StateObject * StateObject::nextState() const
{
    return m_next ? m_next : state().emptyState();
}

void StateObject::setNextState(StateObject *next_state, bool is_bidirectional)
{
    m_next = next_state;

    if (is_bidirectional && next_state) next_state->setPreviousState(this, false);
}

QVariant StateObject::var() const
{
    return m_var;
}

void StateObject::setVar(const QVariant &var)
{
    m_var = var;
}

//state watcher class
StateWatcher::StateWatcher(QObject *parent) : QObject(parent)
{

}

//state manager class
StateManager::StateManager(QObject *parent) : QObject(parent),
    m_empty_state(new EmptyStateObject(this)),
    m_empty_watcher(new StateWatcher(this))
{
}

StateManager & StateManager::instance()
{
    static StateManager manager;
    return manager;
}

EmptyStateObject * StateManager::emptyState() const
{
    return m_empty_state;
}

StateWatcher * StateManager::emptyWatcher() const
{
    return m_empty_watcher;
}

StateObject * StateManager::regist(QObject *context, const QString &state_name, bool is_current)
{
    //create hash
    QHash<QString, StateObject *> *state_hash = m_states.value(context, nullptr);
    if (!state_hash) {
        state_hash = new QHash<QString, StateObject *>();
        m_states.insert(context, state_hash);
        m_watcher_hash.insert(context, new StateWatcher());
    }

    //create state
    StateObject *state = state_hash->value(state_name, nullptr);
    if (!state) {
        state = new StateObject(state_name, nullptr, nullptr, this);
        state_hash->insert(state_name, state);
    }

    //save current
    if (is_current) m_cur_state_hash.insert(context, state);

    return state;
}

bool StateManager::regist(QObject *context, const QStringList &state_names, bool first_is_current, ConnectionType connection_type)
{
    if (state_names.size() < 1) return false;

    //create first
    StateObject *first_state = regist(context, state_names.first(), first_is_current);
    QHash<QString, StateObject *> *state_hash = m_states.value(context, nullptr);
    if (!state_hash) return false; //should not

    //create states
    StateObject *last_state = first_state;
    StateObject *cur_state = last_state;
    for (int i = 1; i < state_names.size(); ++i) {
        QString state_name = state_names.at(i);

        cur_state = state_hash->value(state_name, nullptr);
        if (!cur_state) {
            cur_state = new StateObject(state_name, nullptr, nullptr, this);
            state_hash->insert(state_name, cur_state);
        }

        switch (connection_type) {
        case NO_CONNECTION:
            break;
        case SEQUENTIAL_CONNECTION:
        case LOOP_CONNECTION:
            last_state->setNextState(cur_state, false);
            cur_state->setPreviousState(last_state, false);
            break;
        case CROSS_CONNECTION:
            break;
        default:
            break;
        }

        last_state = cur_state;
    }

    if (connection_type == LOOP_CONNECTION) {
        cur_state->setNextState(first_state, false);
        first_state->setPreviousState(cur_state, false);
    }

    return true;
}

bool StateManager::cancel(QObject *context, const QString &state_name)
{
    QHash<QString, StateObject *> *state_hash = m_states.value(context, nullptr);
    if (!state_hash) return false;

    //remove all
    if (state_name.isEmpty()) {
        qDeleteAll(*state_hash);
        delete state_hash;
        m_states.remove(context);

        delete m_watcher_hash.value(context);
        m_watcher_hash.remove(context);

        m_cur_state_hash.remove(context);
        return true;
    }

    //remove state
    StateObject *state = state_hash->value(state_name, nullptr);
    if (!state) return false;

    state_hash->remove(state_name);
    delete state;

    if (state_hash->isEmpty()) {
        //remove empty hash
        delete state_hash;
        m_states.remove(context);

        delete m_watcher_hash.value(context);
        m_watcher_hash.remove(context);

        m_cur_state_hash.remove(context);
    } else if (m_cur_state_hash.value(context, nullptr) == state) {
        //remove current
        m_cur_state_hash.remove(context);
    } else {
        //auto check hash, todo think a better way
        foreach (QString state_name, state_hash->keys()) {
            StateObject *state_obj = state_hash->value(state_name);
            if (state_obj->previousState() == state) state_obj->setPreviousState(nullptr);
            if (state_obj->nextState() == state) state_obj->setNextState(nullptr);
        }
    }

    return true;
}

StateObject * StateManager::get(QObject *context, const QString &state_name) const
{
    QHash<QString, StateObject *> *state_hash = m_states.value(context, nullptr);
    return state_hash ? state_hash->value(state_name, emptyState()) : emptyState();
}

StateObject * StateManager::current(QObject *context) const
{
    return m_cur_state_hash.value(context, emptyState());
}

void StateManager::change(QObject *context, StateObject *state_obj)
{
    if (!state_obj) return;

    StateObject *cur_state = current(context);
    if (!cur_state->isEmptyState()) cur_state->raus(state_obj);

    m_cur_state_hash.insert(context, state_obj);

    if (!state_obj->isEmptyState()) state_obj->rein(cur_state);

    StateWatcher *cur_watcher = m_watcher_hash.value(context, nullptr);
    if (cur_watcher) cur_watcher->changed(cur_state, state_obj);
}

void StateManager::change(QObject *context, const QString &state_name)
{
    change(context, get(context, state_name));
}

void StateManager::toNext(QObject *context)
{
    change(context, current(context)->nextState());
}

void StateManager::toPrevious(QObject *context)
{
    change(context, current(context)->previousState());
}

StateWatcher * StateManager::watcher(QObject *context) const
{
    return m_watcher_hash.value(context, emptyWatcher());
}
//output function
StateManager & state()
{
    return StateManager::instance();
}
