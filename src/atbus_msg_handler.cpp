#include <sstream>

#include "common/string_oprs.h"

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
        m.init(ATBUS_CMD_NODE_PING, 0, 0, seq);
        protocol::ping_data* ping = m.body.make_body(m.body.ping);
        if (NULL == ping) {
            return EN_ATBUS_ERR_MALLOC;
        }

        ping->time_point = (n.get_timer_sec() / 1000) * 1000 + (n.get_timer_usec() / 1000) % 1000;

        return send_msg(n, conn, m);
    }


    int msg_handler::send_reg(int32_t msg_id, node& n, connection& conn, int32_t ret_code, uint32_t seq) {
        if (msg_id != ATBUS_CMD_NODE_REG_REQ && msg_id != ATBUS_CMD_NODE_REG_RSP) {
            return EN_ATBUS_ERR_PARAMS;
        }

        protocol::msg m;
        m.init(static_cast<ATBUS_PROTOCOL_CMD>(msg_id), 0, ret_code, 0 == seq? n.alloc_msg_seq(): seq);

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
        endpoint* ep = NULL;
        int32_t res = EN_ATBUS_ERR_SUCCESS;
        int32_t rsp_code = EN_ATBUS_ERR_SUCCESS;
        do {
            if (NULL != m.body.reg || NULL == conn) {
                rsp_code = EN_ATBUS_ERR_BAD_DATA;
                break;
            }

            // 如果连接已经设定了端点，不需要再绑定到endpoint
            if (conn->is_connected()) {
                ep = conn->get_binding();
                if (NULL == ep || ep->get_id() != m.body.reg->bus_id) {
                    n.on_error(ep, conn, EN_ATBUS_ERR_ATNODE_BUS_ID_NOT_MATCH, 0);
                    conn->reset();
                    rsp_code = EN_ATBUS_ERR_ATNODE_BUS_ID_NOT_MATCH;
                    break;
                }

                break;
            }

            // 老端点新增连接不需要创建新连接
            ep = n.get_endpoint(m.body.reg->bus_id);
            if (NULL != ep) {
                // 有共享物理机限制的连接只能加为数据节点（一般就是内存通道或者共享内存通道）
                res = ep->add_connection(conn, conn->check_flag(connection::flag_t::ACCESS_SHARE_HOST));
                if (res < 0) {
                    n.on_error(ep, conn, res, 0);
                }
                rsp_code = res;
                break;
            }

            // 创建新端点时需要判定全局路由表权限
            if (n.is_child_node(m.body.reg->bus_id)) {
                if(m.body.reg->has_global_tree && false == n.get_self_endpoint()->get_flag(endpoint::flag_t::GLOBAL_ROUTER)) {
                    rsp_code = EN_ATBUS_ERR_ACCESS_DENY;
                    break;
                }
            }

            endpoint::ptr_t new_ep = endpoint::create(&n, m.body.reg->bus_id, m.body.reg->children_id_mask, m.body.reg->pid, m.body.reg->hostname);
            if (!new_ep) {
                n.on_error(NULL, conn, EN_ATBUS_ERR_MALLOC, 0);
                rsp_code = EN_ATBUS_ERR_MALLOC;
                break;
            }
            ep = new_ep.get();

            res = n.add_endpoint(new_ep);
            if (res < 0) {
                n.on_error(ep, conn, res, 0);
                conn->disconnect();
                rsp_code = res;
                break;
            }
            ep->set_flag(endpoint::flag_t::GLOBAL_ROUTER, m.body.reg->has_global_tree);

            // 新的endpoint要建立所有连接
            ep->add_connection(conn, false);
            for (size_t i = 0; i < m.body.reg->channels.size(); ++i) {
                const protocol::channel_data& chan = m.body.reg->channels[i];
                res = n.connect(chan.address.c_str(), ep);
                if (res < 0) {
                    n.on_error(ep, conn, res, 0);
                } else {
                    ep->add_listen(chan.address);
                }
            }

            // 加入检测列表
            n.add_check_list(new_ep);
        } while (false);

        return send_reg(ATBUS_CMD_NODE_REG_RSP, n, *conn, rsp_code, m.head.sequence);
    }

    int msg_handler::on_recv_node_reg_rsp(node& n, connection* conn, protocol::msg& m, int status, int errcode) {
        if (NULL == conn) {
            return EN_ATBUS_ERR_BAD_DATA;
        }

        if (m.head.ret < 0) {
            endpoint* ep = conn->get_binding();
            if (NULL == ep) {
                n.add_check_list(ep->watch());
            }

            n.on_error(ep, conn, m.head.ret, errcode);

            // 如果是父节点回的错误注册包，则要关闭进程
            if (conn->get_address().address == n.get_conf().father_address) {
                conn->disconnect();
                n.shutdown(m.head.ret);
            } else {
                conn->disconnect();
            }
            
            return m.head.ret;
        }

        return EN_ATBUS_ERR_SUCCESS;
    }

    int msg_handler::on_recv_node_conn_syn(node& n, connection* conn, protocol::msg&, int status, int errcode) {
        return EN_ATBUS_ERR_SUCCESS;
    }

    int msg_handler::on_recv_node_ping(node& n, connection* conn, protocol::msg& m, int status, int errcode) {
        // 复制sequence
        m.init(ATBUS_CMD_NODE_PONG, 0, 0, m.head.sequence);

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
            if (NULL != ep && m.head.sequence == ep->get_stat_ping()) {
                ep->set_stat_ping(0);

                time_t time_point = (n.get_timer_sec() / 1000) * 1000 + (n.get_timer_usec() / 1000) % 1000;
                ep->set_stat_ping_delay(time_point - m.body.ping->time_point);
            }
        }

        return EN_ATBUS_ERR_SUCCESS;
    }
}
