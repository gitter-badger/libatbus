#include "detail/buffer.h"

#include "atbus_node.h"
#include "atbus_msg_handler.h"

#include "detail/libatbus_protocol.h"

namespace atbus {

    int msg_handler::send_ping(node& n, connection& conn) {
        int ret = 0;

        protocol::msg m;
        m.init(ATBUS_CMD_NODE_PING, 0, 0);
        protocol::ping_data* ping = m.body.make_body(m.body.ping);
        if (NULL == ping) {
            return EN_ATBUS_ERR_MALLOC;
        }

        ping->ping_id;
        ping->time_point = n.get_timer_sec();

        return ret;
    }

}
