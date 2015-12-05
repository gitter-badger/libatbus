/**
 * libatbus.h
 *
 *  Created on: 2015年11月20日
 *      Author: owent
 */

#pragma once

#ifndef LIBATBUS_ENDPOINT_H_
#define LIBATBUS_ENDPOINT_H_

#include <list>

#include "std/smart_ptr.h"
#include "design_pattern/noncopyable.h"

#include "detail/libatbus_error.h"
#include "detail/libatbus_config.h"
#include "detail/libatbus_channel_export.h"

#include "atbus_connection.h"

namespace atbus {
    namespace detail {
        template<typename TKey, typename TVal>
        struct auto_select_map {
            typedef ATBUS_ADVANCE_TYPE_MAP(TKey, TVal) type;
        };

        template<typename TVal>
        struct auto_select_set {
            typedef ATBUS_ADVANCE_TYPE_SET(TVal) type;
        };
    }

    class node;

    class endpoint: public util::design_pattern::noncopyable {
    public:
        typedef ATBUS_MACRO_BUSID_TYPE bus_id_t;
        typedef std::shared_ptr<endpoint> ptr_t;

        typedef struct {
            enum type {
                RESETTING,          /** 正在执行重置（防止递归死循环） **/
                CONNECTION_SORTED,
                MAX
            };
        } flag_t;

    private:
        endpoint();

    public:
        /**
         * @brief 创建端点
         */
        static ptr_t create(node* owner, bus_id_t id, uint32_t children_mask, int32_t pid, const std::string& hn);
        ~endpoint();

        void reset();

        inline bus_id_t get_id() const { return id_; }
        inline uint32_t get_children_mask() const { return children_mask_; }

        inline int32_t get_pid() const { return pid_; };
        inline const std::string& get_hostname() const { return hostname_; };


        bool is_child_node(bus_id_t id) const;
        bool is_brother_node(bus_id_t id, uint32_t father_mask) const;
        static bool is_parent_node(bus_id_t id, bus_id_t father_id, uint32_t father_mask);
        static bus_id_t get_children_min_id(bus_id_t id, uint32_t mask);
        static bus_id_t get_children_max_id(bus_id_t id, uint32_t mask);

        bool add_connection(connection* conn, bool force_data);

        bool remove_connection(connection* conn);

    private:
        static bool sort_connection_cmp_fn(const connection::ptr_t& left, const connection::ptr_t& right);

    public:
        connection* get_ctrl_connection(endpoint* ep) const;

        connection* get_data_connection(endpoint* ep) const;
    private:
        bus_id_t id_;
        uint32_t children_mask_;
        std::bitset<flag_t::MAX> flags_;
        std::string hostname_;
        int32_t pid_;

        // 这里不用智能指针是为了该值在上层对象（node）析构时仍然可用
        node* owner_;
        std::weak_ptr<endpoint> watcher_;

        connection::ptr_t ctrl_conn_;
        std::list<connection::ptr_t> data_conn_;
    };
}

#endif /* LIBATBUS_ENDPOINT_H_ */
