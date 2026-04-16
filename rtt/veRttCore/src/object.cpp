#include <ve/rtt/object.h>

namespace imol {

// --- Object ---

struct Object::Private
{
    std::string name;
    Object* parent = nullptr;
    HashMap<int, HashMap<Object*, List<Task>>> connections;
};

Object::Object(const std::string& name) : _p(new Private)
{
    _p->name = name.empty() ? IMOL_UNDEFINED_OBJECT_NAME : name;
}

Object::~Object()
{
    trigger(OBJECT_DELETED);
    if (auto mgr = dynamic_cast<Manager*>(parent())) mgr->remove(this, false);
    delete _p;
}

std::string Object::name() const { return _p->name; }
void Object::setName(const std::string& name) { _p->name = name; }

Object* Object::parent() const { return _p->parent; }
void Object::setParent(Object* obj) { _p->parent = obj; }

void Object::connect(int signal, Object* observer, const Task& action)
{
    if (!_p->connections.has(signal)) {
        _p->connections[signal] = {{observer, {action}}};
    } else if (!_p->connections[signal].has(observer)) {
        _p->connections[signal][observer] = {action};
    } else {
        _p->connections[signal][observer].push_back(action);
    }
    if (observer && observer != this && signal != OBJECT_DELETED) {
        observer->connect(OBJECT_DELETED, this, [=] { disconnect(observer); });
        this->connect(OBJECT_DELETED, observer, [=] { observer->disconnect(this); });
    }
}

void Object::disconnect(int signal, Object* observer)
{
    if (_p->connections.has(signal)) _p->connections[signal].erase(observer);
}

void Object::disconnect(Object* observer)
{
    for (auto& kv : _p->connections) {
        kv.second.erase(observer);
    }
}

bool Object::hasConnection(int signal, Object* observer) const
{
    return _p->connections.has(signal) &&
           _p->connections[signal].has(observer);
}

void Object::trigger(int signal)
{
    if (!_p->connections.has(signal)) return;
    auto copy = _p->connections[signal];
    for (auto& kv : copy) {
        for (auto& action : kv.second) {
            action();
        }
    }
}

// --- Manager ---

Manager::Manager(const std::string& name) : Object(name) {}

Manager::~Manager()
{
    for (auto& kv : *this) {
        Object* obj = kv.second;
        obj->setParent(nullptr);
        delete obj;
    }
}

Object* Manager::add(Object* obj, bool delete_if_failed)
{
    if (!obj) return nullptr;
    if (has(obj->name())) {
        if (delete_if_failed) delete obj;
        return nullptr;
    }
    obj->setParent(this);
    (*this)[obj->name()] = obj;
    return obj;
}

bool Manager::remove(Object* obj, bool auto_delete)
{
    if (!obj) return false;
    bool found = erase(obj->name()) > 0;
    if (!found) {
        for (const auto& kv : *this) {
            if (obj == kv.second) {
                found = erase(kv.first) > 0;
                break;
            }
        }
    }
    if (found) {
        obj->setParent(nullptr);
        if (auto_delete) delete obj;
    }
    return found;
}

bool Manager::remove(const std::string& name, bool auto_delete)
{
    Object* obj = get(name);
    if (!obj || erase(name) == 0) return false;
    obj->setParent(nullptr);
    if (auto_delete) delete obj;
    return true;
}

Object* Manager::get(const std::string& key) const
{
    auto it = find(key);
    return it == end() ? nullptr : it->second;
}

void Manager::fixObjectLinks()
{
    for (auto& kv : *this) {
        kv.second->setName(kv.first);
        kv.second->setParent(this);
    }
}

} // namespace imol
