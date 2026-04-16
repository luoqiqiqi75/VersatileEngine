#pragma once

#include "container.h"

namespace imol {

using Task = std::function<void()>;

class Manager; // forward

class Object {
public:
    explicit Object(const std::string& name = "");
    virtual ~Object();

    std::string name() const;
    void setName(const std::string& name);

    Object* parent() const;
    void setParent(Object* obj);

    enum Signal : int { OBJECT_DELETED = 0x00ff };

    void connect(int signal, Object* observer, const Task& action);
    void disconnect(int signal, Object* observer);
    void disconnect(Object* observer);
    bool hasConnection(int signal, Object* observer) const;
    void trigger(int signal);

private:
    struct Private;
    Private* _p;

    friend class Manager;
};

class Manager : public Object, public HashMap<std::string, Object*> {
public:
    explicit Manager(const std::string& name);
    virtual ~Manager();

    Object* add(Object* obj, bool delete_if_failed = false);
    template<typename SubObj>
    typename std::enable_if<std::is_base_of<Object, SubObj>::value, SubObj*>::type
    add(SubObj* obj, bool delete_if_failed = false) {
        return add(static_cast<Object*>(obj), delete_if_failed) ? obj : nullptr;
    }

    bool remove(Object* obj, bool auto_delete = true);
    bool remove(const std::string& name, bool auto_delete = true);

    Object* get(const std::string& key) const;
    template<class SubObj>
    typename std::enable_if<std::is_base_of<Object, SubObj>::value, SubObj*>::type
    get(const std::string& key) const {
        return dynamic_cast<SubObj*>(get(key));
    }

    void fixObjectLinks();
};

// Creator — factory registry: string → factory function
template<class Signature>
class Creator : public Object, public HashMap<std::string, std::function<Signature>> {
public:
    typedef std::function<Signature> FunctionT;
    typedef FInfo<FunctionT> FInfoT;
    typedef typename FInfoT::RetT RetT;

    explicit Creator(const std::string& name) : Object(name) {}
    virtual ~Creator() {}

    template<typename... Params>
    RetT exec(const std::string& key, Params&&... params) {
        return this->has(key) ? (*this)[key](std::forward<Params>(params)...) : (RetT)(0);
    }
    RetT exec(const std::string& key) {
        return this->has(key) ? (*this)[key]() : (RetT)(0);
    }
};

template<typename T, typename... Ts>
T& instance(Ts&&... ts) { static T t(std::forward<Ts>(ts)...); return t; }

} // namespace imol
