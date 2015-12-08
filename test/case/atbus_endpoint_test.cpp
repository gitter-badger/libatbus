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
    atbus::endpoint::bus_id_t tested = atbus::endpoint::get_children_max_id(0x12345678, 16);
    CASE_EXPECT_EQ(tested, 0x1234FFFF);

    tested = atbus::endpoint::get_children_min_id(0x12345678, 16);
    CASE_EXPECT_EQ(tested, 0x12340000);
}

CASE_TEST(atbus_endpoint, is_child)
{
    atbus::node::conf_t conf;
    atbus::node::default_conf(&conf);
    conf.children_mask = 16;

    atbus::node::ptr_t node = atbus::node::create();
    node->init(0x12345678, &conf);


    // 0值边界检测
    CASE_EXPECT_TRUE(node->is_child_node(0x12340000));
    CASE_EXPECT_TRUE(node->is_child_node(0x1234FFFF));
    CASE_EXPECT_FALSE(node->is_child_node(0x1233FFFF));
    CASE_EXPECT_FALSE(node->is_child_node(0x12350000));

    // 自己是自己的子节点
    CASE_EXPECT_TRUE(node->is_child_node(node->get_id()));
}

CASE_TEST(atbus_endpoint, is_brother)
{
    uint32_t fake_mask = 24;
    atbus::node::conf_t conf;
    atbus::node::default_conf(&conf);
    conf.children_mask = 16;

    atbus::node::ptr_t node = atbus::node::create();
    node->init(0x12345678, &conf);

    // TODO 0值边界检测
    // 自己不是自己的兄弟节点
    CASE_EXPECT_FALSE(node->get_self_endpoint()->is_brother_node(node->get_id(), fake_mask));

    //       F               F
    //      / \             / \
    //    [A]  B          [A]  F
    //                        / \
    //                       X   B
    // 兄弟节点的子节点仍然是兄弟节点
    CASE_EXPECT_TRUE(node->get_self_endpoint()->is_brother_node(0x12335678, fake_mask));

    //       B
    //      / \
    //    [A]  X
    // 父节点是兄弟节点
    CASE_EXPECT_TRUE(node->get_self_endpoint()->is_brother_node(0x12000001, fake_mask));
    
    
    //      [A]
    //      / \
    //     B   X
    // 子节点不是兄弟节点
    CASE_EXPECT_FALSE(node->get_self_endpoint()->is_brother_node(0x12340001, fake_mask));

    //         F
    //        / \
    //       F   B
    //      / \
    //    [A]  X
    // 父节点的兄弟节点不是兄弟节点
    CASE_EXPECT_FALSE(node->get_self_endpoint()->is_brother_node(0x11345678, fake_mask));
}

CASE_TEST(atbus_endpoint, is_father)
{
    // TODO 0值边界检测
}

CASE_TEST(atbus_endpoint, get_connection)
{
    // TODO 排除未完成连接
}