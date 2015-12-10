#include <cstdio>
#include <assert.h>
#include <ctime>
#include <stdint.h>
#include <cstddef>
#include <cstring>
#include <cstdlib>

#include "detail/buffer.h"

#include "atbus_endpoint.h"


#include "detail/libatbus_protocol.h"

namespace atbus {
    endpoint::ptr_t endpoint::create(node* owner, bus_id_t id, uint32_t children_mask, int32_t pid, const std::string& hn) {
        if (NULL == owner) {
            return endpoint::ptr_t();
        }

        endpoint::ptr_t ret(new endpoint());
        if (!ret) {
            return ret;
        }

        ret->id_ = id;
        ret->children_mask_ = children_mask;
        ret->pid_ = pid;
        ret->hostname_ = hn;

        ret->owner_ = owner;
        ret->watcher_ = ret;
        return ret;
    }

    endpoint::endpoint():id_(0), children_mask_(0), pid_(0), owner_(NULL) {
        flags_.reset();
    }

    endpoint::~endpoint() {
        reset();
    }

    void endpoint::reset() {
        // 这个函数可能会在析构时被调用，这时候不能使用watcher_.lock()
        if (flags_.test(flag_t::RESETTING)) {
            return;
        }
        flags_.set(flag_t::RESETTING, true);

        // 释放连接
        if(ctrl_conn_) {
            ctrl_conn_->reset();
            ctrl_conn_.reset();
        }
        for (std::list<connection::ptr_t>::iterator iter = data_conn_.begin(); iter != data_conn_.end(); ++ iter) {
            if(*iter) {
                (*iter)->reset();
            }
        }
        data_conn_.clear();

        // 从父节点移除


        flags_.reset();
        // 只要endpoint存在，则它一定存在于owner_的某个位置。
        // 并且这个值只能在创建时指定，所以不能重置这个值
    }

    bool endpoint::is_child_node(bus_id_t id) const {
        // 目前id是整数，直接位运算即可
        bus_id_t mask = ~((1 << children_mask_) - 1);
        return (id & mask) == (id_ & mask);
    }

    bool endpoint::is_brother_node(bus_id_t id, uint32_t father_mask) const {
        // 兄弟节点的子节点也视为兄弟节点
        // 目前id是整数，直接位运算即可
        bus_id_t c_mask = ~((1 << children_mask_) - 1);
        bus_id_t f_mask = ~((1 << father_mask) - 1);
        // 同一父节点下，且子节点域不同
        return (id & c_mask) != (id_ & c_mask) && (0 == father_mask || (id & f_mask) == (id_ & f_mask));
    }

    bool endpoint::is_parent_node(bus_id_t id, bus_id_t father_id, uint32_t father_mask) {
        bus_id_t mask = ~((1 << father_mask) - 1);
        return id == father_id;
    }

    endpoint::bus_id_t endpoint::get_children_min_id(bus_id_t id, uint32_t mask) {
        bus_id_t maskv = (1 << mask) - 1;
        return id & (~maskv);
    }

    endpoint::bus_id_t endpoint::get_children_max_id(bus_id_t id, uint32_t mask) {
        bus_id_t maskv = (1 << mask) - 1;
        return id | maskv;
    }

    bool endpoint::add_connection(connection* conn, bool force_data) {
        if (!conn) {
            return false;
        }

        if (flags_.test(flag_t::RESETTING)) {
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
            flags_.set(flag_t::CONNECTION_SORTED, false); // 置为未排序状态
        } else {
            ctrl_conn_ = conn->watcher_.lock();
        }

        conn->binding_ = this;
        return true;
    }

    bool endpoint::remove_connection(connection* conn) {
        if (!conn) {
            return false;
        }

        assert(this == conn->binding_);

        // 重置流程会在reset里清理对象，不需要再进行一次查找
        if (flags_.test(flag_t::RESETTING)) {
            conn->reset();
            conn->binding_ = NULL;
            return true;
        }

        if (conn == ctrl_conn_.get()) {
            // 控制节点离线则直接下线
            reset();
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

                // 数据节点全部离线也直接下线
                if (data_conn_.empty()) {
                    reset();
                }
                return true;
            }
        }

        return false;
    }

    bool endpoint::sort_connection_cmp_fn(const connection::ptr_t& left, const connection::ptr_t& right) {
        if (left->check_flag(connection::flag_t::ACCESS_SHARE_ADDR) != right->check_flag(connection::flag_t::ACCESS_SHARE_ADDR)) {
            return left->check_flag(connection::flag_t::ACCESS_SHARE_ADDR);
        }

        if (left->check_flag(connection::flag_t::ACCESS_SHARE_HOST) != right->check_flag(connection::flag_t::ACCESS_SHARE_HOST)) {
            return left->check_flag(connection::flag_t::ACCESS_SHARE_HOST);
        }

        return false;
    }

    connection* endpoint::get_ctrl_connection(endpoint* ep) const {
        if (NULL == ep) {
            return NULL;
        }

        if (this == ep) {
            return NULL;
        }

        if (ep->ctrl_conn_ && connection::state_t::CONNECTED == ep->ctrl_conn_->get_status()) {
            return ep->ctrl_conn_.get();
        }

        return NULL;
    }

    connection* endpoint::get_data_connection(endpoint* ep) const {
        if (NULL == ep) {
            return NULL;
        }

        if (this == ep) {
            return NULL;
        }

        bool share_pid = false, share_host = false;
        if (ep->get_hostname() == get_hostname()) {
            share_host = true;
            if (ep->get_pid() == get_pid()) {
                share_pid = true;
            }
        }

        // 按性能邮件及排序mem>shm>fd
        if (false == ep->flags_.test(flag_t::CONNECTION_SORTED)) {
            ep->data_conn_.sort(sort_connection_cmp_fn);
            ep->flags_.set(flag_t::CONNECTION_SORTED, true);
        }

        for (std::list<connection::ptr_t>::iterator iter = ep->data_conn_.begin(); iter != ep->data_conn_.end(); ++iter) {
            if (connection::state_t::CONNECTED != (*iter)->get_status()) {
                continue;
            }

            if (share_pid && (*iter)->check_flag(connection::flag_t::ACCESS_SHARE_ADDR)) {
                return (*iter).get();
            }

            if (share_host && (*iter)->check_flag(connection::flag_t::ACCESS_SHARE_HOST)) {
                return (*iter).get();
            }

            if (!(*iter)->check_flag(connection::flag_t::ACCESS_SHARE_HOST)) {
                return (*iter).get();
            }
        }

        return get_ctrl_connection(ep);
    }
}
