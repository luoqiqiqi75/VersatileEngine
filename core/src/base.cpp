#include "ve/core/base.h"

#if defined(__clang__) && defined(__has_include)
#if __has_include(<cxxabi.h>)
#define PRIVATE_HAS_CXXABI
#endif
#elif defined(__GLIBCXX__) || defined(__GLIBCPP__)
#define PRIVATE_HAS_CXXABI
#endif

#ifdef PRIVATE_HAS_CXXABI
#include <cxxabi.h>
#include <cstdlib>
#include <cstddef>
#endif

#include <cmath>

namespace ve {

constexpr double eps = 0.000001;
const double pi = 3.1415920;
const double deg2rad = pi / 180;
const double rad2deg = 180 / pi;

namespace basic {
std::string _t_demangle(const char *type_name)
{
#ifdef HAS_CXXABI_H
    int status = 0;
    std::size_t size = 0;
    const char* demangle_name = abi::__cxa_demangle(type_name, NULL, &size, &status);
#else
    const char* demangle_name = type_name;
#endif
    std::string s(demangle_name);
#ifdef HAS_CXXABI_H
    std::free((void*)demangle_name);
#endif
    return s;
}
}

Values::Unit Values::unit() const { return m_unit; }
Values& Values::setUnit(Unit unit) { if (m_unit != SAME) m_unit = unit; return *this; }

Values& Values::add(double d)
{
    std::for_each(begin(), end(), [=] (double& it) { it += d; });
    return *this;
}

Values& Values::multiply(double d, Values::Unit new_unit)
{
    std::for_each(begin(), end(), [=] (double& it) { it *= d; });
    return setUnit(new_unit);
}

Values& Values::m2mm() { return multiply(1000.0, MM); }
Values& Values::mm2m() { return multiply(0.001, M); }
Values& Values::degree2rad() { return multiply(deg2rad, RAD); }
Values& Values::rad2degree() { return multiply(rad2deg, DEGREE); }

bool Values::smallerThan(const Values& other) const
{
    for (int i = 0; i < std::min(sizeAsInt(), other.sizeAsInt()); i++) {
        if (at(i) >= other.at(i)) return false;
    }
    return true;
}

bool Values::greaterThan(const Values &other) const
{
    for (int i = 0; i < std::min(sizeAsInt(), other.sizeAsInt()); i++) {
        if (at(i) <= other.at(i)) return false;
    }
    return true;
}

bool Values::equals(const Values& other) const
{
    int s = sizeAsInt();
    if (s != other.sizeAsInt()) return false;
    for (int i = 0; i < s; i++) {
        if (std::fabs(at(i) - other.at(i)) > eps) return false;
    }
    return true;
}

class Object::Private
{
public:
    std::string name;
    Object* parent = nullptr;
    HashMap<int, HashMap<Object*, List<ActionT>>> connections;
};

Object::Object(const std::string& name) : _p(new Private)
{
    _p->name = name.empty() ? VE_UNDEFINED_OBJECT_NAME : name;
}

Object::~Object()
{
    trigger(OBJECT_DELETED);

    if (auto mgr = dynamic_cast<Manager*>(parent())) mgr->remove(this, false);
    delete _p;
}

Object* Object::parent() const { return _p->parent; }
void Object::setParent(Object *obj) { _p->parent = obj; }

std::string Object::name() const { return _p->name; }

bool Object::hasConnection(int signal, Object* observer)
{
    return _p->connections.has(signal) && _p->connections[signal].has(observer);
}

void Object::connect(int signal, Object* observer, const ActionT& action)
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

void Object::disconnect(Object *observer)
{
    for (auto& kv : _p->connections) {
        kv.second.erase(observer);
    }
}

void Object::trigger(int signal)
{
    if (!_p->connections.has(signal)) return;
    auto hashmap = _p->connections[signal]; // copy needed
    for (auto& kv : hashmap) {
        if (auto obj = kv.first) {
            // todo performance control
            (void)obj;
        }
        for (auto& it : kv.second) {
            it();
        }
    }
}

Manager::Manager(const std::string &name) : Object(name)
{
}

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

bool Manager::remove(Object *obj, bool auto_delete)
{
    if (!obj) return false;
    bool find = erase(obj->name()) > 0;
    if (!find) {
        for (const auto& kv : *this) {
            if (obj == kv.second) {
                find = erase(kv.first) > 0;
                break;
            }
        }
    }
    if (find) {
        obj->setParent(nullptr);
        if (auto_delete) delete obj;
    }
    return false;
}

bool Manager::remove(const std::string &name, bool auto_delete)
{
    Object* obj = get(name);
    if (!obj || erase(name) == 0) return false;
    if (auto_delete) delete obj;
    return true;
}

Object* Manager::get(const std::string &key) const
{
    auto it = find(key);
    return it == end() ? nullptr : it->second;
}

void Manager::fixObjectLinks()
{
    for (auto& kv : *this) {
        kv.second->_p->name = kv.first;
        kv.second->setParent(this);
    }
}

}
