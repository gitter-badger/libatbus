/**
 * libatbus.h
 *
 *  Created on: 2015年11月20日
 *      Author: owent
 */

#pragma once

#ifndef LIBATBUS_ENDPOINT_H_
#define LIBATBUS_ENDPOINT_H_

#include <string>

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

        endpoint();
        ~endpoint();

        int init(std::weak_ptr<node> owner, bus_id_t id, uint32_t children_mask);

        void reset();

        inline bus_id_t get_id() const { return id_; }
        inline uint32_t get_children_mask() const { return children_mask_; }

        bool is_child_node(bus_id_t id) const;
        bool is_brother_node(bus_id_t id, bus_id_t father_id, uint32_t father_mask) const;
        bool is_parent_node(bus_id_t id, bus_id_t father_id, uint32_t father_mask) const;
    private:
        bus_id_t id_;
        uint32_t children_mask_;
        std::weak_ptr<node> owner_;
    };
}

#endif /* LIBATBUS_ENDPOINT_H_ */
