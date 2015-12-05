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

#include <detail/libatbus_error.h>
#include <atbus_node.h>
#include <atbus_endpoint.h>
#include "frame/test_macros.h"


CASE_TEST(atbus_endpoint, get_children_min_max)
{
}

CASE_TEST(atbus_endpoint, is_child)
{
    // TODO 0值边界检测
    // TODO 自己是自己的子节点
}

CASE_TEST(atbus_endpoint, is_brother)
{
    // TODO 0值边界检测
    // TODO 自己是自己的兄弟节点
}

CASE_TEST(atbus_endpoint, is_father)
{
    // TODO 0值边界检测
}

CASE_TEST(atbus_endpoint, get_connection)
{
    // TODO 排除为完成连接
}