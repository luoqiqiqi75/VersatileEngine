#include "ve/core/data.h"

#include <filesystem>

namespace ve {

struct AbstractData::Private
{
    Object* l = nullptr;
    std::uint8_t c = 0;
};

AbstractData::AbstractData() : _p(new Private) {}

AbstractData::~AbstractData()
{
    delete _p->l;
    delete _p;
}

Object* AbstractData::listener() { return _p->l ? _p->l : (_p->l = new Object("d")); }
std::uint8_t AbstractData::control() const { return _p->c; }
std::uint8_t AbstractData::control(short c) { return _p->c = c; }

DataList* DataList::insertEmptyList(int index) { return insertAt(index, DataList())->ptr(); }
DataDict* DataList::insertEmptyDict(int index) { return insertAt(index, DataDict())->ptr(); }
DataList* DataList::appendEmptyList() { return insertEmptyList(sizeAsInt()); }
DataDict* DataList::appendEmptyDict() { return insertEmptyDict(sizeAsInt()); }

DataList* DataDict::insertEmptyList(const std::string& key) { return insertAt(key, DataList())->ptr(); }
DataDict* DataDict::insertEmptyDict(const std::string& key) { return insertAt(key, DataDict())->ptr(); }

// --- DataManager ---

struct DataManager::Private
{
    Dict<AbstractData*> tmp;
};

DataManager::DataManager() : _p(new Private) {}
DataManager::~DataManager() { delete _p; }

Strings DataManager::keys() const { return _p->tmp.keys(); }
Strings DataManager::keys(const std::string& prefix) const
{
    Strings ss;
    for (auto& s : _p->tmp.keys()) {
        if (s.find(prefix) == 0) ss.append(std::move(s));
    }
    return ss;
}

AbstractData* DataManager::insert(const std::string& key, AbstractData* d, bool auto_replace)
{
    _p->tmp.insertOne(key, d);
    return d;
}

AbstractData* DataManager::remove(const std::string& key, bool auto_delete)
{
    auto d = _p->tmp.value(key, nullptr);
    if (!d) return nullptr;
    _p->tmp.erase(key);
    if (auto_delete) delete d;
    return d;
}

static AbstractData* internal_rfind_sep(const Dict<AbstractData*>& c, const std::string& k, std::size_t& p)
{
    p = k.rfind('.', p);
    if (p == std::string::npos) return nullptr;
    while ((k[p] == '.' || k[p] == ' ') && p > 0) p--;
    if (auto d = c.value(k.substr(0, p + 1))) return d;
    return p > 1 ? internal_rfind_sep(c, k, p) : nullptr;
}

static AbstractData* internal_find_frag(AbstractData* df, const std::string& kf, std::size_t& p)
{
    AbstractData* d = df;
    do {
        if (auto dl = dynamic_cast<TypedData<DataList>*>(d)) {
            if (kf[p] != '#') break;
            std::size_t pn = kf.find('.', p);
            std::string s = kf.substr(p, pn == std::string::npos ? pn : pn - p);
            if (d = dl->ref().rawDataAt(std::stoi(s.substr(1)))) {
                p += s.length() + 1;
            } else {
                break;
            }
        } else if (auto dd = dynamic_cast<TypedData<DataDict>*>(d)) {
            std::size_t pn = kf.find('.', p);
            std::string s = kf.substr(p, pn == std::string::npos ? pn : pn - p);
            if (d = dd->ref().rawDataAt(s)) {
                p += s.length() + 1;
            } else {
                break;
            }
        } else {
            break;
        }
    } while (p < kf.length());
    return d;
}

AbstractData* DataManager::find(const std::string& key) const
{
    std::string rest;
    auto d = find(key, rest);
    return rest.empty() ? d : nullptr;
}

AbstractData* DataManager::find(const DataList* data_list, const std::string& key) const
{
    if (key.empty() || key[0] != '#') return nullptr;
    std::size_t p = 0;
    std::size_t pn = key.find('.');
    std::string s = key.substr(p, pn == std::string::npos ? pn : pn - p);
    AbstractData* d = nullptr;
    if (d = data_list->rawDataAt(std::stoi(s.substr(1)))) {
        p += s.length() + 1;
    } else {
        return nullptr;
    }
    d = internal_find_frag(d, key, p);
    return p >= key.length() ? d : nullptr;
}

AbstractData* DataManager::find(const DataDict* data_dict, const std::string& key) const
{
    std::size_t p = 0;
    std::size_t pn = key.find('.');
    std::string s = key.substr(p, pn == std::string::npos ? pn : pn - p);
    AbstractData* d = nullptr;
    if (d = data_dict->rawDataAt(s)) {
        p += s.length() + 1;
    } else {
        return nullptr;
    }
    d = internal_find_frag(d, key, p);
    return p >= key.length() ? d : nullptr;
}

AbstractData* DataManager::find(AbstractData* root, const std::string& key) const
{
    std::size_t ps = 0;
    auto d = internal_find_frag(root, key, ps);
    return ps >= key.length() ? d : nullptr;
}

AbstractData* DataManager::find(const std::string& key, std::string& rest) const
{
    if (auto d = _p->tmp.value(key, nullptr)) {
        rest.clear();
        return d;
    }

    if (key.length() > 2) {
        std::size_t p = key.length() - 2;
        if (auto d = internal_rfind_sep(_p->tmp, key, p)) {
            if (p < key.length() - 1) {
                rest = key.substr(p + 2);
                if (rest.length() > 0) {
                    std::size_t ps = 0;
                    d = internal_find_frag(d, rest, ps);
                    if (!d) {
                        rest = key;
                    } else if (ps > 0) {
                        rest = ps >= rest.length() ? "" : rest.substr(ps);
                    }
                }
            }
            return d;
        }
    }
    rest = key;
    return nullptr;
}

DataManager& globalDataManager()
{
    static DataManager i;
    return i;
}

namespace data {

TypedData<void>* createVoid(const std::string& key, bool* ok)
{
    return globalDataManager().insertNew(key, new TypedData<void>, ok);
}

TypedData<void>* createTrigger(const std::string& key, const Object::ActionT& action, bool trigger_now)
{
    return createTrigger(key, nullptr, std::move(action), trigger_now);
}

TypedData<void>* createTrigger(const std::string& key, Object* observer, const Object::ActionT& action, bool trigger_now)
{
    bool ok = false;
    auto d = createVoid(key, &ok);
    if (!ok) return nullptr;
    if (trigger_now) action(Var());
    d->listener()->connect<DATA_CHANGED>(observer ? observer : d->listener(), std::move(action));
    return d;
}

} // namespace data

}
