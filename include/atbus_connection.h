/**
 * libatbus.h
 *
 *  Created on: 2015年11月20日
 *      Author: owent
 */

#pragma once

#ifndef LIBATBUS_CONNECTION_H_
#define LIBATBUS_CONNECTION_H_

#include <bitset>
#include <ctime>
#include <list>

#include "std/smart_ptr.h"
#include "design_pattern/noncopyable.h"

#include "detail/libatbus_error.h"
#include "detail/libatbus_config.h"
#include "detail/libatbus_channel_export.h"

namespace atbus {
    class connection: public util::design_pattern::noncopyable {
    public:
        typedef struct {
            enum type {
                DISCONNECTED = 0,   /** 未连接 **/
                CONNECTING,         /** 正在连接 **/
                HANDSHAKING,        /** 正在握手 **/
                CONNECTED,          /** 已连接 **/
            };
        } state_t;

    private:
    };
}

#endif /* LIBATBUS_CONNECTION_H_ */
