#include <cstdio>
#include <assert.h>
#include <ctime>
#include <stdint.h>
#include <cstddef>
#include <cstring>
#include <cstdlib>

#include "atbus_proto_generated.h"

#include "detail/buffer.h"

#include "atbus_endpoint.h"

namespace atbus {
    endpoint::endpoint():id_(0), children_mask_(0) {}

    endpoint::~endpoint() {
        reset();
    }

    int endpoint::init(std::weak_ptr<node> owner, bus_id_t id, uint32_t children_mask) {
        id_ = id;
        children_mask_ = children_mask;

        owner_ = owner;
        return 0;
    }

    void endpoint::reset() {
        id_ = 0;
        children_mask_ = 0;

        owner_.reset();
    }

    bool endpoint::is_child_node(bus_id_t id) const {
        // 目前id是整数，直接位运算即可
        bus_id_t mask = ~((1 << children_mask_) - 1);
        return (id & mask) == (id_ & mask);
    }

    bool endpoint::is_brother_node(bus_id_t id, bus_id_t father_id, uint32_t father_mask) const {
        // 目前id是整数，直接位运算即可
        bus_id_t c_mask = ~((1 << children_mask_) - 1);
        bus_id_t f_mask = ~((1 << father_mask) - 1);
        // 同一父节点下，且子节点域不同
        return (id & c_mask) != (id_ & c_mask) && (id & f_mask) == (id_ & f_mask);
    }

    bool endpoint::is_parent_node(bus_id_t id, bus_id_t father_id, uint32_t father_mask) const {
        return id == father_id;
    }
}
