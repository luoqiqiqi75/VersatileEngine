// dynamic.cpp — ve::dds Dynamic Types implementation

#include "ve/ros/dds/dynamic.h"

namespace ve::dds {

namespace ftypes = eprosima::fastrtps::types;
namespace fdds   = eprosima::fastdds::dds;

namespace {

inline bool hasNodeValue(const Node* node)
{
    return node && !node->get().isNull();
}

} // namespace

// ============================================================================
// buildDynType
// ============================================================================

ftypes::DynamicType_ptr buildDynType(Node* schema, const std::string& type_name)
{
    auto* factory = ftypes::DynamicTypeBuilderFactory::get_instance();
    auto name = type_name.empty() ? schema->name() : type_name;
    if (name.empty()) name = "VeStruct";

    auto builder = factory->create_struct_builder();
    builder->set_name(name);

    ftypes::MemberId mid = 0;
    for (auto* child : *schema) {
        auto cname = child->name();
        if (cname.empty()) continue;

        if (child->count() > 0 && !hasNodeValue(child)) {
            auto nested = buildDynType(child, cname);
            builder->add_member(mid++, cname, nested);
            continue;
        }

        auto val = child->get();
        switch (val.type()) {
        case Var::BOOL:
            builder->add_member(mid++, cname,
                factory->create_bool_type());
            break;
        case Var::INT:
            builder->add_member(mid++, cname,
                factory->create_int64_type());
            break;
        case Var::DOUBLE:
            builder->add_member(mid++, cname,
                factory->create_float64_type());
            break;
        case Var::STRING:
            builder->add_member(mid++, cname,
                factory->create_string_type());
            break;
        default:
            builder->add_member(mid++, cname,
                factory->create_string_type());
            break;
        }
    }

    return builder->build();
}

// ============================================================================
// nodeToData — write Node values into DynamicData
// ============================================================================

void nodeToData(Node* n, ftypes::DynamicData* d)
{
    ftypes::MemberId mid = 0;
    for (auto* child : *n) {
        auto cname = child->name();
        if (cname.empty()) { ++mid; continue; }

        if (child->count() > 0 && !hasNodeValue(child)) {
            auto* nested = d->loan_value(mid);
            if (nested) {
                nodeToData(child, nested);
                d->return_loaned_value(nested);
            }
            ++mid;
            continue;
        }

        auto val = child->get();
        switch (val.type()) {
        case Var::BOOL:
            d->set_bool_value(val.toBool(), mid);
            break;
        case Var::INT:
            d->set_int64_value(val.toInt(), mid);
            break;
        case Var::DOUBLE:
            d->set_float64_value(val.toDouble(), mid);
            break;
        case Var::STRING:
            d->set_string_value(val.toString(), mid);
            break;
        default:
            d->set_string_value(val.toString(), mid);
            break;
        }
        ++mid;
    }
}

// ============================================================================
// dataToNode — read DynamicData into Node values
// ============================================================================

void dataToNode(ftypes::DynamicData* d, Node* n)
{
    ftypes::MemberId mid = 0;
    for (auto* child : *n) {
        auto cname = child->name();
        if (cname.empty()) { ++mid; continue; }

        if (child->count() > 0 && !hasNodeValue(child)) {
            auto* nested = d->loan_value(mid);
            if (nested) {
                dataToNode(nested, child);
                d->return_loaned_value(nested);
            }
            ++mid;
            continue;
        }

        auto val = child->get();
        switch (val.type()) {
        case Var::BOOL: {
            bool v = false;
            d->get_bool_value(v, mid);
            child->set(Var(v));
            break;
        }
        case Var::INT: {
            int64_t v = 0;
            d->get_int64_value(v, mid);
            child->set(Var(v));
            break;
        }
        case Var::DOUBLE: {
            double v = 0.0;
            d->get_float64_value(v, mid);
            child->set(Var(v));
            break;
        }
        case Var::STRING: {
            std::string v;
            d->get_string_value(v, mid);
            child->set(Var(std::move(v)));
            break;
        }
        default: {
            std::string v;
            d->get_string_value(v, mid);
            child->set(Var(std::move(v)));
            break;
        }
        }
        ++mid;
    }
}

// ============================================================================
// DynPublisher
// ============================================================================

struct DynPublisher::Private
{
    Participant* participant = nullptr;
    std::string  topic;
    Node*        schema = nullptr;
    ftypes::DynamicType_ptr dynType;
    ftypes::DynamicData*    data = nullptr;
};

DynPublisher::DynPublisher(Participant& p, const std::string& topic, Node* schema)
    : _p(new Private)
{
    _p->participant = &p;
    _p->topic  = topic;
    _p->schema = schema;

    _p->dynType = buildDynType(schema, topic);

    fdds::TypeSupport ts(new ftypes::DynamicPubSubType(_p->dynType));
    p.createTopicGroup(Participant::TopicGroupInfo{
        topic,
        {"", fdds::TypeSupport()},
        {"rt/" + topic, std::move(ts)},
        nullptr
    });

    _p->data = ftypes::DynamicDataFactory::get_instance()
        ->create_data(_p->dynType);
}

DynPublisher::~DynPublisher()
{
    if (_p->data) {
        ftypes::DynamicDataFactory::get_instance()->delete_data(_p->data);
    }
    delete _p;
}

void DynPublisher::publish(Node* data)
{
    if (!_p->data || !data) return;
    nodeToData(data, _p->data);

    auto* w = _p->participant->writer(_p->topic);
    if (w) w->write(reinterpret_cast<void*>(_p->data));
}

// ============================================================================
// DynSubscriber
// ============================================================================

struct DynSubscriber::Private
{
    Participant* participant = nullptr;
    std::string  topic;
    Node*        schema = nullptr;
    ftypes::DynamicType_ptr dynType;
    ftypes::DynamicData*    data = nullptr;
    DynSubscriber::Handler  handler;
    Node*                   bridgeTarget = nullptr;
    Node                    tempNode;
};

DynSubscriber::DynSubscriber(Participant& p, const std::string& topic, Node* schema)
    : _p(new Private)
{
    _p->participant = &p;
    _p->topic  = topic;
    _p->schema = schema;

    _p->dynType = buildDynType(schema, topic);

    fdds::TypeSupport ts(new ftypes::DynamicPubSubType(_p->dynType));

    auto* self = this;
    p.createTopicGroup(Participant::TopicGroupInfo{
        topic,
        {"rt/" + topic, std::move(ts)},
        {"", fdds::TypeSupport()},
        [self](Participant::DataReaderT* r, Participant::DataWriterT*) -> int {
            fdds::SampleInfo si;
            auto* data = self->_p->data;
            if (!data) return -1;

            while (r->get_unread_count() > 0) {
                auto rc = r->take_next_sample(data, &si);
                if (rc != eprosima::fastrtps::types::ReturnCode_t::RETCODE_OK)
                    continue;
                if (si.instance_state != fdds::ALIVE_INSTANCE_STATE)
                    continue;

                if (self->_p->bridgeTarget) {
                    dataToNode(data, self->_p->bridgeTarget);
                } else if (self->_p->handler) {
                    dataToNode(data, &self->_p->tempNode);
                    self->_p->handler(&self->_p->tempNode);
                }
            }
            return 0;
        }
    });

    _p->data = ftypes::DynamicDataFactory::get_instance()
        ->create_data(_p->dynType);
}

DynSubscriber::~DynSubscriber()
{
    if (_p->data) {
        ftypes::DynamicDataFactory::get_instance()->delete_data(_p->data);
    }
    delete _p;
}

void DynSubscriber::onReceive(Handler h)
{
    _p->handler = std::move(h);
}

void DynSubscriber::bridgeTo(Node* target)
{
    _p->bridgeTarget = target;
}

} // namespace ve::dds
