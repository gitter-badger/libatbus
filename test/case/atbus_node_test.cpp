#include <iostream>
#include <sstream>
#include <cstdlib>
#include <cstring>

#include "common/string_oprs.h"

#ifdef max
#undef max
#endif

#ifdef min
#undef min
#endif

#include <atbus_node.h>

#include "detail/libatbus_protocol.h"

#include "frame/test_macros.h"

CASE_TEST(atbus_node, basic_test)
{
    atbus::protocol::msg m_src, m_dst;
    std::string packed_buffer;
    char test_buffer[] = "hello world!";

    {
        m_src.init(ATBUS_CMD_DATA_TRANSFORM_REQ, 123, 0, 13);
        m_src.body.make_forward(456, 789, test_buffer, sizeof(test_buffer));
        m_src.body.forward->router.push_back(210);

        std::stringstream ss;
        msgpack::pack(ss, m_src);
        packed_buffer = ss.str();
        std::stringstream so;
        util::string::serialization(packed_buffer.data(), packed_buffer.size(), so);
        CASE_MSG_INFO() << "msgpack encoded(size="<< packed_buffer.size()<<"): " << so.str() << std::endl;
    }

    msgpack::unpacked result;
    {
        msgpack::unpack(result, packed_buffer.data(), packed_buffer.size());
        msgpack::object obj = result.get();
        CASE_EXPECT_FALSE(obj.is_nil());
        obj.convert(m_dst);
    }

    {
        CASE_EXPECT_EQ(ATBUS_CMD_DATA_TRANSFORM_REQ, m_dst.head.cmd);
        CASE_EXPECT_EQ(123, m_dst.head.type);
        CASE_EXPECT_EQ(0, m_dst.head.ret);
        CASE_EXPECT_EQ(13, m_dst.head.sequence);

        CASE_EXPECT_EQ(456, m_dst.body.forward->from);
        CASE_EXPECT_EQ(789, m_dst.body.forward->to);
        CASE_EXPECT_EQ(210, m_dst.body.forward->router.front());
        CASE_EXPECT_EQ(0, UTIL_STRFUNC_STRNCMP(test_buffer, reinterpret_cast<const char*>(m_dst.body.forward->content.ptr), sizeof(test_buffer)));
    }
}

CASE_TEST(atbus_node, child_endpoint_opr)
{
    atbus::node::conf_t conf;
    atbus::node::default_conf(&conf);
    conf.children_mask = 16;

    atbus::node::ptr_t node = atbus::node::create();
    node->init(0x12345678, &conf);


    atbus::endpoint::ptr_t ep = atbus::endpoint::create(node.get(), 0x12345679, 8, node->get_pid(), node->get_hostname());
    // 插入到末尾
    CASE_EXPECT_EQ(0, node->add_endpoint(ep));
    CASE_EXPECT_EQ(1, node->get_children().size());

    // 插入到中间
    ep = atbus::endpoint::create(node.get(), 0x12345589, 8, node->get_pid(), node->get_hostname());
    CASE_EXPECT_EQ(0, node->add_endpoint(ep));
    CASE_EXPECT_EQ(2, node->get_children().size());

    // 新端点子域冲突-父子关系
    ep = atbus::endpoint::create(node.get(), 0x12345680, 4, node->get_pid(), node->get_hostname());
    CASE_EXPECT_EQ(EN_ATBUS_ERR_ATNODE_INVALID_ID, node->add_endpoint(ep));

    // 新端点子域冲突-子父关系
    ep = atbus::endpoint::create(node.get(), 0x12345780, 12, node->get_pid(), node->get_hostname());
    CASE_EXPECT_EQ(EN_ATBUS_ERR_ATNODE_INVALID_ID, node->add_endpoint(ep));
    ep = atbus::endpoint::create(node.get(), 0x12345480, 12, node->get_pid(), node->get_hostname());
    CASE_EXPECT_EQ(EN_ATBUS_ERR_ATNODE_INVALID_ID, node->add_endpoint(ep));

    // 新端点子域冲突-ID不同子域相同
    ep = atbus::endpoint::create(node.get(), 0x12345680, 8, node->get_pid(), node->get_hostname());
    CASE_EXPECT_EQ(EN_ATBUS_ERR_ATNODE_INVALID_ID, node->add_endpoint(ep));

    // 移除失败-找不到
    CASE_EXPECT_EQ(EN_ATBUS_ERR_ATNODE_NOT_FOUND, node->remove_endpoint(0x12345680));
    // 移除成功
    CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, node->remove_endpoint(0x12345589));
}

// TODO 主动reset流程测试
// TODO 被动析构流程测试
// TODO 注册成功流程测试
// TODO 注册到父节点失败导致下线的流程测试
// TODO 注册到子节点失败不会导致下线的流程测试

// TODO 父节点断线重连测试
// TODO 兄弟节点断线重连测试
// TODO 子节点断线后重新注册测试
// TODO 连接过程中的转态检查

// TODO 定时Ping Pong协议测试
// TODO 自定义命令协议测试

// TODO 父子节点消息转发测试
// TODO 兄弟节点消息转发测试
// TODO 兄弟节点通过父节点转发消息并建立直连测试（测试路由）
// TODO 兄弟节点通过多层父节点转发消息并不会建立直连测试
// TODO 直连节点发送失败测试
// TODO 发送给子节点转发失败的回复通知测试
// TODO 发送给父节点转发失败的回复通知测试
// TODO 发送给已下线兄弟节点并失败的回复通知测试（网络失败）


// TODO 全量表第一次拉取测试
// TODO 全量表通知给父节点和子节点测试
