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
        m_src.init(ATBUS_CMD_DATA_TRANSFORM_REQ, 123, 0);
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

        CASE_EXPECT_EQ(456, m_dst.body.forward->from);
        CASE_EXPECT_EQ(789, m_dst.body.forward->to);
        CASE_EXPECT_EQ(210, m_dst.body.forward->router.front());
        CASE_EXPECT_EQ(0, UTIL_STRFUNC_STRNCMP(test_buffer, reinterpret_cast<const char*>(m_dst.body.forward->content.ptr), sizeof(test_buffer)));
    }
}

CASE_TEST(atbus_node, child_endpoint_opr)
{
    // TODO 插入到末尾
    // TODO 插入到中间

    // TODO 新端点子域冲突-父子关系
    // TODO 新端点子域冲突-子父关系
    // TODO 新端点子域冲突-ID不同子域相同

    // TODO 移除失败-找不到
    // TODO 移除失败-不匹配
    // TODO 移除成功
}

