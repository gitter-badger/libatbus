#include <iostream>
#include <fstream>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <map>
#include <atomic>
#include <memory>
#include <limits>
#include <numeric>

#include "common/string_oprs.h"

#ifdef max
#undef max
#endif

#ifdef min
#undef min
#endif

#include "atbus_proto_generated.h"
#include <atbus_node.h>

#include "frame/test_macros.h"

CASE_TEST(atbus_node, flatbuffer_address_align)
{
    char magic_str[] = "hello world! some short message!";
    flatbuffers::FlatBufferBuilder fbb;

    atbus::protocol::Createmsg(fbb);
    flatbuffers::Offset<atbus::protocol::msg_head> head;
    flatbuffers::Offset<atbus::protocol::msg_body> body;
    // header
    {
        atbus::protocol::msg_headBuilder mhb(fbb);
        mhb.add_cmd(atbus::protocol::CMD_CMD_DATA_TRANSFORM_REQ);
        mhb.add_ret(0);
        mhb.add_type(123);
        head = mhb.Finish();
    }
    // body
    {
        flatbuffers::Offset<flatbuffers::Vector<int8_t> > content = fbb.CreateVector<int8_t>(reinterpret_cast<const int8_t*>(magic_str), sizeof(magic_str));
        atbus::protocol::forward_dataBuilder mbfdb(fbb);
        mbfdb.add_from(123);
        mbfdb.add_to(456);
        mbfdb.add_content(content);
        flatbuffers::Offset<atbus::protocol::forward_data> fd = mbfdb.Finish();

        atbus::protocol::msg_bodyBuilder mbb(fbb);
        mbb.add_forward(fd);
        body = mbb.Finish();
    }

    atbus::protocol::msgBuilder mb(fbb);
    mb.add_head(head);
    mb.add_body(body);
    fbb.Finish(mb.Finish());

    // 非对齐内存测试
    std::string str;
    str.resize(fbb.GetSize() + 1);

    memcpy(&str[1], fbb.GetBufferPointer(), fbb.GetSize());

    const atbus::protocol::msg* dm = atbus::protocol::Getmsg(&str[1]);
    CASE_EXPECT_EQ(atbus::protocol::CMD_CMD_DATA_TRANSFORM_REQ, dm->head()->type());
    CASE_EXPECT_EQ(0, dm->head()->ret());
    CASE_EXPECT_EQ(123, dm->head()->type());
    CASE_EXPECT_EQ(123, dm->body()->forward()->from());
    CASE_EXPECT_EQ(456, dm->body()->forward()->to());
    CASE_EXPECT_EQ(0, UTIL_STRFUNC_STRCMP(magic_str, reinterpret_cast<const char*>(dm->body()->forward()->content()->Data())));
}

CASE_TEST(atbus_node, basic_test)
{
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

