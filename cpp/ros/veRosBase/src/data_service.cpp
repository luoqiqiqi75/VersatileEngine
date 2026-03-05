#include "ve/ros/service/data_service.h"

namespace hemera::service::standard {

template<> std::string dataList<SERIALIZE_YAML>(const std::string& str)
{
    return ve::globalDataManager().keys(str).tostring(", ");
}

YAML::Node search_key_in_yaml(YAML::Node n, const std::string& k, std::string& e)
{
    std::size_t pos = 0;
    do {
        std::size_t sep = k.find('.', pos);
        std::string sub = k.substr(pos, sep == std::string::npos ? sep : sep - pos);
        if (sub.empty()) {
            e = "@illegal: empty key";
            return YAML::Node();
        } else if (sub[0] == '#') {
            int i = std::stoi(sub.substr(1));
            if (!n.IsSequence() || n.size() <= i) {
                e = "@illegal: invalid index";
                return YAML::Node();
            }
            n.reset(n[i]);
        } else {
            n.reset(n[sub]);
            if (n.IsNull()) {
                e = "@illegal: invalid key";
                return YAML::Node();
            }
        }
        pos += sub.length() + 1;
    } while (pos < k.length());
    return n;
}

template<> std::string dataGet<SERIALIZE_YAML>(const std::string& str)
{
    std::string rest;
    auto d = ve::globalDataManager().find(str, rest);
    if (!d) return "@unexist";
    auto y = d->toYaml();
    if (!rest.empty() && !y.IsNull()) {
        std::string e;
        y = search_key_in_yaml(y, rest, e);
        if (!e.empty()) return e;
    }
    y.SetStyle(YAML::EmitterStyle::Flow);
    std::ostringstream oss;
    oss << y;
    return oss.str();
}

template<> std::string dataSet<SERIALIZE_YAML>(const std::string& str)
{
    std::size_t p = str.find('=');
    if (p == std::string::npos) {
        if (auto d = ve::globalDataManager().find(str)) {
            if (auto vd = dynamic_cast<ve::TypedData<void>*>(d)) {
                return vd->update() ? "@success" : "@failed";
            } else {
                return "@illegal: non trigger";
            }
        } else {
            return "@unexist";
        }
    } else if (p == 0 || str.size() < 3) {
        return "@illegal";
    }
    std::size_t pl = p - 1, pr = p + 1;
    while (str[pl] == ' ' && pl > 0) pl--;
    while (str[pr] == ' ' && pr < str.size()) pr++;
    if (pl == 0 || pr == str.size()) return "@illegal";

    std::string rest;
    auto d = ve::globalDataManager().find(str.substr(0, pl + 1), rest);
    if (!d) return "@unexist";
    try {
        if (rest.empty()) {
            auto y = YAML::Load(str.substr(pr));
            if (d->fromYaml(y)) return "@success";
        } else {
            auto n = d->toYaml();
            std::string e;
            auto sub_n = search_key_in_yaml(n, rest, e);
            if (!e.empty()) return e;
            sub_n = YAML::Load(str.substr(pr));
            if (d->fromYaml(n)) return "@success";
        }
    } catch (const std::exception& e) {
        return std::string("@failed: ") + e.what();
    }
    return "@failed";
}

}
