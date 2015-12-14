/**
 * atbus_msg_handler.h
 *
 *  Created on: 2015年12月14日
 *      Author: owent
 */

#pragma once

#ifndef LIBATBUS_MSG_HANDLER_H_
#define LIBATBUS_MSG_HANDLER_H_

#include <bitset>
#include <ctime>
#include <list>

namespace atbus {
    namespace protocol {
        struct msg;
    }

    class node;
    class endpoint;
    class connection;

    struct msg_handler {    
        static int send_ping(node& n, connection& conn);
    };
}

#endif /* LIBATBUS_MSG_HANDLER_H_ */
