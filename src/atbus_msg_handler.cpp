#include <sstream>

#include "detail/buffer.h"

#include "atbus_node.h"
#include "atbus_msg_handler.h"

#include "detail/libatbus_protocol.h"

namespace atbus {

    int msg_handler::dispatch_msg(node& n, connection* conn, protocol::msg* m, int status, int errcode) {
        static handler_fn_t fns[ATBUS_CMD_MAX] = { NULL };
        if (NULL == fns[ATBUS_CMD_DATA_TRANSFORM_REQ]) {
            fns[ATBUS_CMD_DATA_TRANSFORM_REQ] = msg_handler::on_recv_data_transfer_req;
            fns[ATBUS_CMD_DATA_TRANSFORM_RSP] = msg_handler::on_recv_data_transfer_rsp;

            fns[ATBUS_CMD_CUSTOM_CMD_REQ] = msg_handler::on_recv_custom_cmd_req;

            fns[ATBUS_CMD_NODE_SYNC_REQ] = msg_handler::on_recv_node_sync_req;
            fns[ATBUS_CMD_NODE_SYNC_RSP] = msg_handler::on_recv_node_sync_rsp;
            fns[ATBUS_CMD_NODE_REG_REQ] = msg_handler::on_recv_node_reg_req;
            fns[ATBUS_CMD_NODE_REG_RSP] = msg_handler::on_recv_node_reg_rsp;
            fns[ATBUS_CMD_NODE_CONN_SYN] = msg_handler::on_recv_node_conn_syn;
            fns[ATBUS_CMD_NODE_PING] = msg_handler::on_recv_node_ping;
            fns[ATBUS_CMD_NODE_PONG] = msg_handler::on_recv_node_pong;
        }

        if (NULL == m) {
            return EN_ATBUS_ERR_BAD_DATA;
        }

        if (m->head.cmd >= ATBUS_CMD_MAX || m->head.cmd <= 0) {
            return EN_ATBUS_ERR_ATNODE_INVALID_MSG;
        }

        if (NULL == fns[m->head.cmd]) {
            return EN_ATBUS_ERR_ATNODE_INVALID_MSG;
        }

        return fns[m->head.cmd](n, conn, *m, status, errcode);
    }

    int msg_handler::send_ping(node& n, connection& conn, uint32_t seq) {
        protocol::msg m;
        m.init(ATBUS_CMD_NODE_PING, 0, 0);
        protocol::ping_data* ping = m.body.make_body(m.body.ping);
        if (NULL == ping) {
            return EN_ATBUS_ERR_MALLOC;
        }

        ping->ping_id = seq;
        ping->time_point = (n.get_timer_sec() / 1000) * 1000 + (n.get_timer_usec() / 1000) % 1000;

        return send_msg(n, conn, m);
    }


    int msg_handler::send_reg(node& n, connection& conn) {
        protocol::msg m;
        m.init(ATBUS_CMD_NODE_REG_REQ, 0, 0);

        protocol::reg_data* reg = m.body.make_body(m.body.reg);
        if (NULL == reg) {
            return EN_ATBUS_ERR_MALLOC;
        }

        reg->bus_id = n.get_id();
        reg->pid = n.get_pid();
        reg->hostname = n.get_hostname();

        for (std::list<std::string>::const_iterator iter = n.get_listen_list().begin(); iter != n.get_listen_list().end(); ++ iter) {
            reg->channels.push_back(protocol::channel_data());
            reg->channels.back().address = *iter;
        }

        reg->children_id_mask = n.get_self_endpoint()->get_children_mask();
        reg->has_global_tree = n.get_self_endpoint()->get_flag(endpoint::flag_t::GLOBAL_ROUTER);

        return send_msg(n, conn, m);
    }

    int msg_handler::send_msg(node& n, connection& conn, const protocol::msg& m) {
        std::stringstream ss;
        msgpack::pack(ss, m);
        std::string packed_buffer = ss.str();

        if (packed_buffer.size() >= n.get_conf().msg_size) {
            return EN_ATBUS_ERR_BUFF_LIMIT;
        }

        return conn.push(packed_buffer.data(), packed_buffer.size());;
    }

    int msg_handler::on_recv_data_transfer_req(node& n, connection* conn, protocol::msg&, int status, int errcode) {
        return EN_ATBUS_ERR_SUCCESS;
    }

    int msg_handler::on_recv_data_transfer_rsp(node& n, connection* conn, protocol::msg&, int status, int errcode) {
        return EN_ATBUS_ERR_SUCCESS;
    }

    int msg_handler::on_recv_custom_cmd_req(node& n, connection* conn, protocol::msg&, int status, int errcode) {
        return EN_ATBUS_ERR_SUCCESS;
    }

    int msg_handler::on_recv_node_sync_req(node& n, connection* conn, protocol::msg&, int status, int errcode) {
        return EN_ATBUS_ERR_SUCCESS;
    }

    int msg_handler::on_recv_node_sync_rsp(node& n, connection* conn, protocol::msg&, int status, int errcode) {
        return EN_ATBUS_ERR_SUCCESS;
    }

    int msg_handler::on_recv_node_reg_req(node& n, connection* conn, protocol::msg& m, int status, int errcode) {
        if (NULL != m.body.reg || NULL == conn) {
            return EN_ATBUS_ERR_BAD_DATA;
        }

        
        return EN_ATBUS_ERR_SUCCESS;
    }

    int msg_handler::on_recv_node_reg_rsp(node& n, connection* conn, protocol::msg&, int status, int errcode) {
        return EN_ATBUS_ERR_SUCCESS;
    }

    int msg_handler::on_recv_node_conn_syn(node& n, connection* conn, protocol::msg&, int status, int errcode) {
        return EN_ATBUS_ERR_SUCCESS;
    }

    int msg_handler::on_recv_node_ping(node& n, connection* conn, protocol::msg& m, int status, int errcode) {
        m.init(ATBUS_CMD_NODE_PONG, 0, 0);

        if (NULL != m.body.ping) {
            return EN_ATBUS_ERR_BAD_DATA;
        }

        if (NULL != conn) {
            endpoint* ep = conn->get_binding();
            if (NULL != ep) {
                return n.send_msg(ep->get_id(), m);
            }
        }

        return EN_ATBUS_ERR_SUCCESS;
    }

    int msg_handler::on_recv_node_pong(node& n, connection* conn, protocol::msg& m, int status, int errcode) {

        if (NULL != m.body.ping) {
            return EN_ATBUS_ERR_BAD_DATA;
        }

        if (NULL != conn) {
            endpoint* ep = conn->get_binding();
            if (NULL != ep && m.body.ping->ping_id == ep->get_stat_ping()) {
                ep->set_stat_ping(0);

                time_t time_point = (n.get_timer_sec() / 1000) * 1000 + (n.get_timer_usec() / 1000) % 1000;
                ep->set_stat_ping_delay(time_point - m.body.ping->time_point);
            }
        }

        return EN_ATBUS_ERR_SUCCESS;
    }
}
