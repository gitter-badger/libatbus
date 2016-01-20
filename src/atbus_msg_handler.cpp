#include <sstream>

#include "common/string_oprs.h"

#include "detail/buffer.h"

#include "atbus_node.h"
#include "atbus_msg_handler.h"

#include "detail/libatbus_protocol.h"

namespace atbus {

    namespace detail {
        const char* get_cmd_name(ATBUS_PROTOCOL_CMD cmd) {
            static std::string fn_names[ATBUS_CMD_MAX];

#define ATBUS_CMD_REG_NAME(x) fn_names[x] = #x

            if (fn_names[ATBUS_CMD_DATA_TRANSFORM_REQ].empty()) {
                ATBUS_CMD_REG_NAME(ATBUS_CMD_DATA_TRANSFORM_REQ);
                ATBUS_CMD_REG_NAME(ATBUS_CMD_DATA_TRANSFORM_RSP);

                ATBUS_CMD_REG_NAME(ATBUS_CMD_CUSTOM_CMD_REQ);

                ATBUS_CMD_REG_NAME(ATBUS_CMD_NODE_SYNC_REQ);
                ATBUS_CMD_REG_NAME(ATBUS_CMD_NODE_SYNC_RSP);
                ATBUS_CMD_REG_NAME(ATBUS_CMD_NODE_REG_REQ);
                ATBUS_CMD_REG_NAME(ATBUS_CMD_NODE_REG_RSP);
                ATBUS_CMD_REG_NAME(ATBUS_CMD_NODE_CONN_SYN);
                ATBUS_CMD_REG_NAME(ATBUS_CMD_NODE_PING);
                ATBUS_CMD_REG_NAME(ATBUS_CMD_NODE_PONG);

                for (int i = 0; i < ATBUS_CMD_MAX; ++i) {
                    if (fn_names[i].empty()) {
                        std::stringstream ss;
                        ss << i;
                        ss >> fn_names[i];
                    } else {
                        fn_names[i] = fn_names[i].substr(10);
                    }
                }
            }

#undef ATBUS_CMD_REG_NAME

            if (cmd >= ATBUS_CMD_MAX) {
                return "Invalid Cmd";
            }

            return fn_names[cmd].c_str();
        }
    }

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

        n.stat_add_dispatch_times();
        return fns[m->head.cmd](n, conn, *m, status, errcode);
    }

    int msg_handler::send_ping(node& n, connection& conn, uint32_t seq) {
        protocol::msg m;
        m.init(n.get_id(), ATBUS_CMD_NODE_PING, 0, 0, seq);
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
        m.init(n.get_id(), static_cast<ATBUS_PROTOCOL_CMD>(msg_id), 0, ret_code, 0 == seq? n.alloc_msg_seq(): seq);

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

    int msg_handler::send_transfer_rsp(node& n, protocol::msg& m, int32_t ret_code) {
        m.init(n.get_id(), ATBUS_CMD_DATA_TRANSFORM_RSP, 0, ret_code, m.head.sequence);
        m.body.forward->to = m.body.forward->from;
        m.body.forward->from = n.get_id();

        return n.send_ctrl_msg(m.body.forward->to, m);
    }

    int msg_handler::send_msg(node& n, connection& conn, const protocol::msg& m) {
        std::stringstream ss;
        msgpack::pack(ss, m);
        std::string packed_buffer = ss.str();

        if (packed_buffer.size() >= n.get_conf().msg_size) {
            return EN_ATBUS_ERR_BUFF_LIMIT;
        }

        ATBUS_FUNC_NODE_DEBUG(n, conn.get_binding(), &conn, 
            "node send msg(cmd=%s, type=%d, length=%llu)", 
            detail::get_cmd_name(m.head.cmd), 
            m.head.type,
            static_cast<unsigned long long>(packed_buffer.size())
       );
        return conn.push(packed_buffer.data(), packed_buffer.size());;
    }

    int msg_handler::on_recv_data_transfer_req(node& n, connection* conn, protocol::msg& m, int status, int errcode) {
        if (NULL == m.body.forward || NULL == conn) {
            ATBUS_FUNC_NODE_ERROR(n, NULL == conn? NULL: conn->get_binding(), conn, EN_ATBUS_ERR_BAD_DATA, 0);
            return EN_ATBUS_ERR_BAD_DATA;
        }

        if (m.body.forward->to == n.get_id()) {
            ATBUS_FUNC_NODE_DEBUG(n, (NULL == conn?NULL: conn->get_binding()), conn, "node recv data length = %lld", static_cast<unsigned long long>(m.body.forward->content.size));
            n.on_recv_data(conn, m.head.type, m.body.forward->content.ptr, m.body.forward->content.size);

            if (m.body.forward->check_flag(atbus::protocol::forward_data::FLAG_REQUIRE_RSP)) {
                return send_transfer_rsp(n, m, EN_ATBUS_ERR_SUCCESS);
            }
            return EN_ATBUS_ERR_SUCCESS;
        }

        if (m.body.forward->router.size() >= static_cast<size_t>(n.get_conf().ttl)) {
            return send_transfer_rsp(n, m, EN_ATBUS_ERR_ATNODE_TTL);
        }

        int res = 0;
        endpoint* to_ep = NULL;
        // 转发数据
        res = n.send_data_msg(m.body.forward->to, m, &to_ep, NULL);

        // 子节点转发成功
        if (res >= 0 && n.is_child_node(m.body.forward->to)) {
            // 如果来源和目标消息都来自于子节点，则通知建立直连
            if (NULL != to_ep && n.is_child_node(m.head.src_bus_id) && n.is_child_node(to_ep->get_id())) {
                protocol::msg conn_syn_m;
                conn_syn_m.init(n.get_id(), ATBUS_CMD_NODE_CONN_SYN, 0, 0, n.alloc_msg_seq());
                protocol::conn_data* new_conn = conn_syn_m.body.make_body(conn_syn_m.body.conn);
                if (NULL == new_conn) {
                    ATBUS_FUNC_NODE_ERROR(n, NULL, NULL, EN_ATBUS_ERR_MALLOC, 0);
                    return send_transfer_rsp(n, m, EN_ATBUS_ERR_MALLOC);
                }

                const std::list<std::string>& listen_addrs = to_ep->get_listen();
                for (std::list<std::string>::const_iterator iter = listen_addrs.begin(); iter != listen_addrs.end(); ++ iter) {
                    // 通知连接控制通道，控制通道不能是（共享）内存通道
                    if (0 != UTIL_STRFUNC_STRNCASE_CMP("mem", iter->c_str(), 3) &&
                        0 != UTIL_STRFUNC_STRNCASE_CMP("shm", iter->c_str(), 3)) {
                        new_conn->address.address = *iter;
                        break;
                    }
                }

                if (!new_conn->address.address.empty()) {
                    return n.send_ctrl_msg(m.head.src_bus_id, conn_syn_m);
                }
            }

            return res;
        }

        // 直接兄弟节点转发失败，并且不来自于父节点，则转发送给父节点(父节点也会被判定为兄弟节点)
        // 如果失败可能是兄弟节点的连接未完成，但是endpoint已建立，所以直接发给父节点
        if (res < 0 && false == n.is_parent_node(m.head.src_bus_id) && n.is_brother_node(m.body.forward->to)) {
            // 如果失败的发送目标已经是父节点则不需要重发
            const endpoint* parent_ep = n.get_parent_endpoint();
            if (NULL != parent_ep && (NULL == to_ep || false == n.is_parent_node(to_ep->get_id()))) {
                res = n.send_data_msg(parent_ep->get_id(), m);
            }
        }

        // 只有失败或请求方要求回包，才下发通知，类似ICMP协议
        if (res < 0 || m.body.forward->check_flag(atbus::protocol::forward_data::FLAG_REQUIRE_RSP)) {
            res = send_transfer_rsp(n, m, res);
        }
        
        if (res < 0) {
            ATBUS_FUNC_NODE_ERROR(n, NULL, NULL, res, 0);
        }

        return res;
    }

    int msg_handler::on_recv_data_transfer_rsp(node& n, connection* conn, protocol::msg& m, int status, int errcode) {
        if (NULL == m.body.forward || NULL == conn) {
            ATBUS_FUNC_NODE_ERROR(n, NULL == conn ? NULL : conn->get_binding(), conn, EN_ATBUS_ERR_BAD_DATA, 0);
            return EN_ATBUS_ERR_BAD_DATA;
        }

        ATBUS_FUNC_NODE_ERROR(n, conn->get_binding(), conn, m.head.ret, 0);

        return EN_ATBUS_ERR_SUCCESS;
    }

    int msg_handler::on_recv_custom_cmd_req(node& n, connection* conn, protocol::msg& m, int status, int errcode) {
        if (NULL == m.body.custom) {
            ATBUS_FUNC_NODE_ERROR(n, NULL == conn ? NULL : conn->get_binding(), conn, EN_ATBUS_ERR_BAD_DATA, 0);
            return EN_ATBUS_ERR_BAD_DATA;
        }

        std::vector<std::pair<const void*, size_t> > cmd_args;
        cmd_args.reserve(m.body.custom->commands.size());
        for (size_t i = 0; i < m.body.custom->commands.size(); ++i) {
            cmd_args.push_back(std::make_pair(m.body.custom->commands[i].ptr, m.body.custom->commands[i].size));
        }

        return n.on_custom_cmd(NULL == conn ? NULL : conn->get_binding(), conn, m.body.custom->from, cmd_args);
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
            if (NULL == m.body.reg || NULL == conn) {
                rsp_code = EN_ATBUS_ERR_BAD_DATA;
                break;
            }

            // 如果连接已经设定了端点，不需要再绑定到endpoint
            if (conn->is_connected()) {
                ep = conn->get_binding();
                if (NULL == ep || ep->get_id() != m.body.reg->bus_id) {
                    ATBUS_FUNC_NODE_ERROR(n, ep, conn, EN_ATBUS_ERR_ATNODE_BUS_ID_NOT_MATCH, 0);
                    conn->reset();
                    rsp_code = EN_ATBUS_ERR_ATNODE_BUS_ID_NOT_MATCH;
                    break;
                }

                ATBUS_FUNC_NODE_DEBUG(n, ep, conn, "connection already connected recv req");
                break;
            }

            // 老端点新增连接不需要创建新连接
            ep = n.get_endpoint(m.body.reg->bus_id);
            if (NULL != ep) {
                // 有共享物理机限制的连接只能加为数据节点（一般就是内存通道或者共享内存通道）
                if (false == ep->add_connection(conn, conn->check_flag(connection::flag_t::ACCESS_SHARE_HOST))) {
                    res = EN_ATBUS_ERR_ATNODE_NO_CONNECTION;
                    ATBUS_FUNC_NODE_ERROR(n, ep, conn, res, 0);
                }
                rsp_code = res;

                ATBUS_FUNC_NODE_DEBUG(n, ep, conn, "connection added to existed endpoint, res: %d", res);
                break;
            }

            // 创建新端点时需要判定全局路由表权限
            if (n.is_child_node(m.body.reg->bus_id)) {
                if(m.body.reg->has_global_tree && false == n.get_self_endpoint()->get_flag(endpoint::flag_t::GLOBAL_ROUTER)) {
                    rsp_code = EN_ATBUS_ERR_ACCESS_DENY;

                    ATBUS_FUNC_NODE_DEBUG(n, ep, conn, "self has no global tree, children reg access deny");
                    break;
                }
            }

            endpoint::ptr_t new_ep = endpoint::create(&n, m.body.reg->bus_id, m.body.reg->children_id_mask, m.body.reg->pid, m.body.reg->hostname);
            if (!new_ep) {
                ATBUS_FUNC_NODE_ERROR(n, NULL, conn, EN_ATBUS_ERR_MALLOC, 0);
                rsp_code = EN_ATBUS_ERR_MALLOC;
                break;
            }
            ep = new_ep.get();

            res = n.add_endpoint(new_ep);
            if (res < 0) {
                ATBUS_FUNC_NODE_ERROR(n, ep, conn, res, 0);
                conn->disconnect();
                rsp_code = res;
                break;
            }
            ep->set_flag(endpoint::flag_t::GLOBAL_ROUTER, m.body.reg->has_global_tree);

            ATBUS_FUNC_NODE_DEBUG(n, ep, conn, "node add a new endpoint, res: %d", res);
            // 新的endpoint要建立所有连接
            ep->add_connection(conn, false);
            for (size_t i = 0; i < m.body.reg->channels.size(); ++i) {
                const protocol::channel_data& chan = m.body.reg->channels[i];
                res = n.connect(chan.address.c_str(), ep);
                if (res < 0) {
                    ATBUS_FUNC_NODE_ERROR(n, ep, conn, res, 0);
                } else {
                    ep->add_listen(chan.address);
                }
            }

            // 加入检测列表
            n.add_check_list(new_ep);
        } while (false);

        // 仅fd连接发回注册回包，否则忽略（内存和共享内存通道为单工通道）
        if (conn->check_flag(connection::flag_t::REG_FD)) {
            return send_reg(ATBUS_CMD_NODE_REG_RSP, n, *conn, rsp_code, m.head.sequence);
        } else {
            return 0;
        }
    }

    int msg_handler::on_recv_node_reg_rsp(node& n, connection* conn, protocol::msg& m, int status, int errcode) {
        if (NULL == conn) {
            return EN_ATBUS_ERR_BAD_DATA;
        }

        endpoint* ep = conn->get_binding();
        n.on_reg(ep, conn, m.head.ret);

        if (m.head.ret < 0) {
            if (NULL == ep) {
                n.add_check_list(ep->watch());
            }

            ATBUS_FUNC_NODE_ERROR(n, ep, conn, m.head.ret, errcode);

            // 如果是父节点回的错误注册包，且未被激活过，则要关闭进程
            conn->disconnect();
            if (conn->get_address().address == n.get_conf().father_address) {
                if (!n.check(node::flag_t::EN_FT_ACTIVED)) {

                    ATBUS_FUNC_NODE_DEBUG(n, ep, conn, "node register to parent node failed, shutdown");
                    n.shutdown(m.head.ret);
                }
            }
            
            return m.head.ret;
        }

        return EN_ATBUS_ERR_SUCCESS;
    }

    int msg_handler::on_recv_node_conn_syn(node& n, connection* conn, protocol::msg& m, int status, int errcode) {
        if (NULL == m.body.conn || NULL == conn) {
            ATBUS_FUNC_NODE_ERROR(n, NULL == conn ? NULL : conn->get_binding(), conn, EN_ATBUS_ERR_BAD_DATA, 0);
            return EN_ATBUS_ERR_BAD_DATA;
        }

        ATBUS_FUNC_NODE_DEBUG(n, NULL, NULL, "node recv conn_syn and prepare connect to %s", m.body.conn->address.address.c_str());
        int ret = n.connect(m.body.conn->address.address.c_str());
        if (ret < 0) {
            ATBUS_FUNC_NODE_ERROR(n, n.get_self_endpoint(), NULL, ret, 0);
        }
        return EN_ATBUS_ERR_SUCCESS;
    }

    int msg_handler::on_recv_node_ping(node& n, connection* conn, protocol::msg& m, int status, int errcode) {
        // 复制sequence
        m.init(n.get_id(), ATBUS_CMD_NODE_PONG, 0, 0, m.head.sequence);

        if (NULL == m.body.ping) {
            return EN_ATBUS_ERR_BAD_DATA;
        }

        if (NULL != conn) {
            endpoint* ep = conn->get_binding();
            if (NULL != ep) {
                ATBUS_FUNC_NODE_DEBUG(n, ep, conn, "node recv ping");
                return n.send_ctrl_msg(ep->get_id(), m);
            }
        }

        return EN_ATBUS_ERR_SUCCESS;
    }

    int msg_handler::on_recv_node_pong(node& n, connection* conn, protocol::msg& m, int status, int errcode) {

        if (NULL == m.body.ping) {
            return EN_ATBUS_ERR_BAD_DATA;
        }

        if (NULL != conn) {
            endpoint* ep = conn->get_binding();
            ATBUS_FUNC_NODE_DEBUG(n, ep, conn, "node recv pong");

            if (NULL != ep && m.head.sequence == ep->get_stat_ping()) {
                ep->set_stat_ping(0);

                time_t time_point = (n.get_timer_sec() / 1000) * 1000 + (n.get_timer_usec() / 1000) % 1000;
                ep->set_stat_ping_delay(time_point - m.body.ping->time_point);
            }
        }

        return EN_ATBUS_ERR_SUCCESS;
    }
}
