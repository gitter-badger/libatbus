/**
 * libatbus.h
 *
 *  Created on: 2015年11月20日
 *      Author: owent
 */

#pragma once

#ifndef LIBATBUS_ENDPOINT_H_
#define LIBATBUS_ENDPOINT_H_

#include <bitset>
#include <ctime>
#include <list>

#include "std/smart_ptr.h"
#include "design_pattern/noncopyable.h"

#include "detail/libatbus_error.h"
#include "detail/libatbus_config.h"
#include "detail/libatbus_channel_export.h"

namespace atbus {
    class node;

    class endpoint: public util::design_pattern::noncopyable {
    public:
        typedef ATBUS_MACRO_BUSID_TYPE bus_id_t;


        inline bus_id_t get_id() const { return id_; }
        inline bus_id_t get_children_mask() const { return children_mask_; }
    private:
        bus_id_t id_;
        uint32_t children_mask_;
        std::weak_ptr<node> owner_;
    };
}

#endif /* LIBATBUS_ENDPOINT_H_ */
