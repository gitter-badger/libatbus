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

#include <atbus_node.h>

#include "detail/libatbus_protocol.h"

#include "frame/test_macros.h"

CASE_TEST(atbus_node, basic_test)
{
    atbus::protocol::forward_data fd;
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

