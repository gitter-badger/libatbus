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
    endpoint::ptr_t endpoint::create(node* owner, bus_id_t id, uint32_t children_mask) {
        if (NULL == owner) {
            return endpoint::ptr_t();
        }

        endpoint::ptr_t ret(new endpoint());
        if (!ret) {
            return ret;
        }

        ret->id_ = id;
        ret->children_mask_ = children_mask;

        ret->owner_ = owner;
        ret->watcher_ = ret;
        return ret;
    }

    endpoint::endpoint():id_(0), children_mask_(0), owner_(NULL) {}

    endpoint::~endpoint() {
        reset();
    }

    void endpoint::reset() {
        // 这个函数可能会在析构时被调用，这时候不能使用watcher_.lock()

        id_ = 0;
        children_mask_ = 0;

        // TODO 从owner移除

        // 只要endpoint存在，则它一定存在于owner_的某个位置。
        // 并且这个值只能在创建时指定，所以不能重置这个值
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

    bool endpoint::is_parent_node(bus_id_t id, bus_id_t father_id, uint32_t father_mask) {
        return id == father_id;
    }

    bool endpoint::add_connection(connection* conn, bool force_data) {
        if (!conn) {
            return false;
        }

        if (this == conn->binding_) {
            return true;
        }

        if (NULL != conn->binding_) {
            return false;
        }

        if (force_data || ctrl_conn_) {
            data_conn_.push_back(conn->watcher_.lock());
        } else {
            ctrl_conn_ = conn->watcher_.lock();
        }

        conn->binding_ = this;
    }

    bool endpoint::remove_connection(connection* conn) {
        if (!conn) {
            return false;
        }

        if (conn == ctrl_conn_.get()) {
            assert(this == conn->binding_);

            conn->reset();

            conn->binding_ = NULL;
            ctrl_conn_.reset();
            return true;
        }

        // 每个节点的连接数不会很多，并且连接断开时是个低频操作
        // 所以O(log(n))的复杂度并没有关系
        for (std::list<connection::ptr_t>::iterator iter = data_conn_.begin(); iter != data_conn_.end(); ++iter) {
            if ((*iter).get() == conn) {
                assert(this == conn->binding_);
                (*iter)->reset();

                conn->binding_ = NULL;
                data_conn_.erase(iter);
                return true;
            }
        }

        return false;
    }
}
