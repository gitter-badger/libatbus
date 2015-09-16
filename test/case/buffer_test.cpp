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
#include <detail/buffer.h>

#include "detail/libatbus_channel_export.h"
#include "frame/test_macros.h"

CASE_TEST(buffer, varint)
{
    char buf[10] = {0};
    uint64_t i = 0;
    uint64_t j = 1;

    size_t res = atbus::detail::fn::write_vint(i, buf, sizeof(buf));
    CASE_EXPECT_EQ(1, res);

    res = atbus::detail::fn::read_vint(j, buf, sizeof(buf));
    CASE_EXPECT_EQ(1, res);
    CASE_EXPECT_EQ(0, j);

    // bound-max
    i = 127;
    res = atbus::detail::fn::write_vint(i, buf, sizeof(buf));
    CASE_EXPECT_EQ(1, res);
    CASE_EXPECT_EQ(buf[1], 0);

    res = atbus::detail::fn::read_vint(j, buf, sizeof(buf));
    CASE_EXPECT_EQ(1, res);
    CASE_EXPECT_EQ(127, j);

    // bound-min
    i = 128;
    res = atbus::detail::fn::write_vint(i, buf, sizeof(buf));
    CASE_EXPECT_EQ(2, res);

    res = atbus::detail::fn::read_vint(j, buf, sizeof(buf));
    CASE_EXPECT_EQ(2, res);
    CASE_EXPECT_EQ(128, j);

    // failed
    i = 2080160; // ‭BIN: 111111011110110100000‬
    res = atbus::detail::fn::write_vint(i, buf, 2);
    CASE_EXPECT_EQ(0, res);

    res = atbus::detail::fn::write_vint(i, buf, 3);
    CASE_EXPECT_EQ(3, res);

    res = atbus::detail::fn::read_vint(j, buf, 2);
    CASE_EXPECT_EQ(0, res);

    res = atbus::detail::fn::read_vint(j, buf, 3);
    CASE_EXPECT_EQ(3, res);
    CASE_EXPECT_EQ(2080160, j);

    // max uint64
    i = UINT64_MAX;
    res = atbus::detail::fn::write_vint(i, buf, sizeof(buf));
    CASE_EXPECT_EQ(10, res);
}

CASE_TEST(buffer, buffer_block)
{
    // size align
    CASE_EXPECT_EQ(
        atbus::detail::buffer_block::full_size(99),
        atbus::detail::buffer_block::head_size(99) + atbus::detail::buffer_block::padding_size(99)
    );

    // malloc
    char buf[4 * 1024] = {0};
    atbus::detail::buffer_block* p = atbus::detail::buffer_block::malloc(99);
    CASE_EXPECT_NE(NULL, p);
    CASE_EXPECT_EQ(99, p->size());

    // size and size extend protect
    p->pop(50);
    CASE_EXPECT_EQ(49, p->size());
    CASE_EXPECT_EQ(99, p->raw_size());
    CASE_EXPECT_EQ(atbus::detail::fn::buffer_step(p->raw_data(), 50), p->data());
    CASE_EXPECT_NE(p->raw_data(), p->data());

    p->pop(100);
    CASE_EXPECT_EQ(0, p->size());
    CASE_EXPECT_EQ(atbus::detail::fn::buffer_step(p->raw_data(), 99), p->data());

    atbus::detail::buffer_block::free(p);

    // failed
    void* next_free = atbus::detail::buffer_block::create(buf, 256, 256);
    CASE_EXPECT_EQ(NULL, next_free);
    for (size_t i = 0; i < sizeof(atbus::detail::buffer_block); ++ i) {
        CASE_EXPECT_EQ(buf[i], 0);
    }

    // data bound
    size_t fs = atbus::detail::buffer_block::full_size(256);
    next_free = atbus::detail::buffer_block::create(buf, sizeof(buf), 256);
    p = reinterpret_cast<atbus::detail::buffer_block*>(buf);
    CASE_EXPECT_EQ(atbus::detail::fn::buffer_step(p->raw_data(), 256), next_free);
    CASE_EXPECT_EQ(atbus::detail::fn::buffer_step(buf, fs), next_free);

    memset(p->data(), -1, p->size());
    CASE_EXPECT_EQ(buf[fs], 0);
    CASE_EXPECT_EQ(buf[fs - 1], -1);
}

CASE_TEST(buffer, dynamic_buffer_manager)
{
    atbus::detail::buffer_manager mgr;
    CASE_EXPECT_TRUE(mgr.empty());

    mgr.set_limit(1023, 10);

    CASE_EXPECT_EQ(1023, mgr.limit().limit_size_);
    CASE_EXPECT_EQ(10, mgr.limit().limit_number_);

    // size limit
    void* pointer;
    void* check_ptr[4];
    int res = mgr.push(pointer, 256);
    CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, res);
    CASE_EXPECT_NE(NULL, pointer);
    res = mgr.push(pointer, 256);
    CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, res);
    CASE_EXPECT_NE(NULL, pointer);
    res = mgr.push(pointer, 256);
    CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, res);
    CASE_EXPECT_NE(NULL, pointer);
    res = mgr.push(pointer, 256);
    CASE_EXPECT_EQ(EN_ATBUS_ERR_BUFF_LIMIT, res);
    CASE_EXPECT_EQ(NULL, pointer);
    res = mgr.push(pointer, 255);
    CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, res);
    CASE_EXPECT_NE(NULL, pointer);

    CASE_EXPECT_EQ(1023, mgr.limit().cost_size_);
    CASE_EXPECT_EQ(4, mgr.limit().cost_number_);

    mgr.reset();
    mgr.set_limit(1023, 3);

    // from empty to full to empty
    // should has the same result
    for (int i = 0; i < 3; ++ i) {
        // number limit
        res = mgr.push(check_ptr[0], 99);
        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, res);
        memset(check_ptr[0], -1, 99);

        res = mgr.push(check_ptr[1], 28);
        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, res);
        memset(check_ptr[1], 0, 28);

        res = mgr.push(check_ptr[2], 17);
        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, res);
        memset(check_ptr[2], -1, 17);

        res = mgr.push(check_ptr[3], 63);
        CASE_EXPECT_EQ(EN_ATBUS_ERR_BUFF_LIMIT, res);

        CASE_EXPECT_EQ(144, mgr.limit().cost_size_);
        CASE_EXPECT_EQ(3, mgr.limit().cost_number_);

        // pop and remove block
        size_t s;
        res = mgr.front(pointer, s);
        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, res);
        CASE_EXPECT_EQ(pointer, check_ptr[0]);
        CASE_EXPECT_EQ(99, s);
        CASE_EXPECT_EQ(*reinterpret_cast<char*>(pointer), -1);

        res = mgr.pop(128);
        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, res);

        CASE_EXPECT_EQ(45, mgr.limit().cost_size_);
        CASE_EXPECT_EQ(2, mgr.limit().cost_number_);

        res = mgr.front(pointer, s);
        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, res);
        CASE_EXPECT_EQ(pointer, check_ptr[1]);
        CASE_EXPECT_EQ(28, s);
        CASE_EXPECT_EQ(*reinterpret_cast<char*>(pointer), 0);

        res = mgr.pop(100);
        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, res);

        CASE_EXPECT_EQ(17, mgr.limit().cost_size_);
        CASE_EXPECT_EQ(1, mgr.limit().cost_number_);

        res = mgr.front(pointer, s);
        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, res);
        CASE_EXPECT_EQ(pointer, check_ptr[2]);
        CASE_EXPECT_EQ(17, s);
        CASE_EXPECT_EQ(*reinterpret_cast<char*>(pointer), -1);

        // pop but not remove block
        mgr.pop(10);
        CASE_EXPECT_EQ(7, mgr.limit().cost_size_);
        CASE_EXPECT_EQ(1, mgr.limit().cost_number_);

        res = mgr.front(pointer, s);
        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, res);
        CASE_EXPECT_EQ(pointer, atbus::detail::fn::buffer_step(check_ptr[2], 10));
        CASE_EXPECT_EQ(7, s);
        CASE_EXPECT_EQ(*reinterpret_cast<char*>(pointer), -1);

        // pop all
        res = mgr.pop(10);
        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, res);
        CASE_EXPECT_TRUE(mgr.empty());
        CASE_EXPECT_EQ(0, mgr.limit().cost_size_);
        CASE_EXPECT_EQ(0, mgr.limit().cost_number_);

        // pop nothing
        res = mgr.front(pointer, s);
        CASE_EXPECT_EQ(EN_ATBUS_ERR_NO_DATA, res);
        CASE_EXPECT_EQ(NULL, pointer);
        CASE_EXPECT_EQ(0, s);

        res = mgr.pop(10);
        CASE_EXPECT_EQ(EN_ATBUS_ERR_NO_DATA, res);
    }
}


CASE_TEST(buffer, static_buffer_manager)
{
    atbus::detail::buffer_manager mgr;
    CASE_EXPECT_TRUE(mgr.empty());

    mgr.set_mode(1023, 10);
    CASE_EXPECT_FALSE(mgr.set_limit(2048, 10));

    CASE_EXPECT_EQ(1023, mgr.limit().limit_size_);
    CASE_EXPECT_EQ(10, mgr.limit().limit_number_);

    // size limit
    void* pointer;
    void* check_ptr[4];
    int res = mgr.push(pointer, 256 - atbus::detail::buffer_block::head_size(256));
    CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, res);
    CASE_EXPECT_NE(NULL, pointer);
    res = mgr.push(pointer, 256 - atbus::detail::buffer_block::head_size(256));
    CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, res);
    CASE_EXPECT_NE(NULL, pointer);
    res = mgr.push(pointer, 256 - atbus::detail::buffer_block::head_size(256));
    CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, res);
    CASE_EXPECT_NE(NULL, pointer);
    res = mgr.push(pointer, 256 + 2 * atbus::detail::buffer_block::head_size(256));
    CASE_EXPECT_EQ(EN_ATBUS_ERR_BUFF_LIMIT, res);
    CASE_EXPECT_EQ(NULL, pointer);
    res = mgr.push(pointer, 257 - atbus::detail::buffer_block::head_size(257));
    CASE_EXPECT_EQ(EN_ATBUS_ERR_BUFF_LIMIT, res);
    CASE_EXPECT_EQ(NULL, pointer);
    res = mgr.push(pointer, 255 - atbus::detail::buffer_block::head_size(255));
    CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, res);
    CASE_EXPECT_NE(NULL, pointer);

    CASE_EXPECT_EQ(1023, mgr.limit().cost_size_ + atbus::detail::buffer_block::head_size(255) + 3 * atbus::detail::buffer_block::head_size(256));
    CASE_EXPECT_EQ(4, mgr.limit().cost_number_);

    mgr.reset();
    mgr.set_mode(1023, 3);
    // from empty to full to empty
    // should has the same result
    for (int i = 0; i < 3; ++ i) {
        // number limit
        res = mgr.push(check_ptr[0], 99);
        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, res);
        memset(check_ptr[0], -1, 99);

        res = mgr.push(check_ptr[1], 28);
        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, res);
        memset(check_ptr[1], 0, 28);

        res = mgr.push(check_ptr[2], 17);
        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, res);
        memset(check_ptr[2], -1, 17);

        res = mgr.push(check_ptr[3], 63);
        CASE_EXPECT_EQ(EN_ATBUS_ERR_BUFF_LIMIT, res);

        CASE_EXPECT_EQ(144, mgr.limit().cost_size_);
        CASE_EXPECT_EQ(3, mgr.limit().cost_number_);

        // pop and remove block
        size_t s;
        res = mgr.front(pointer, s);
        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, res);
        CASE_EXPECT_EQ(pointer, check_ptr[0]);
        CASE_EXPECT_EQ(99, s);
        CASE_EXPECT_EQ(*reinterpret_cast<char*>(pointer), -1);

        res = mgr.pop(128);
        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, res);

        CASE_EXPECT_EQ(45, mgr.limit().cost_size_);
        CASE_EXPECT_EQ(2, mgr.limit().cost_number_);

        res = mgr.front(pointer, s);
        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, res);
        CASE_EXPECT_EQ(pointer, check_ptr[1]);
        CASE_EXPECT_EQ(28, s);
        CASE_EXPECT_EQ(*reinterpret_cast<char*>(pointer), 0);

        res = mgr.pop(100);
        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, res);

        CASE_EXPECT_EQ(17, mgr.limit().cost_size_);
        CASE_EXPECT_EQ(1, mgr.limit().cost_number_);

        res = mgr.front(pointer, s);
        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, res);
        CASE_EXPECT_EQ(pointer, check_ptr[2]);
        CASE_EXPECT_EQ(17, s);
        CASE_EXPECT_EQ(*reinterpret_cast<char*>(pointer), -1);

        // pop but not remove block
        mgr.pop(10);
        CASE_EXPECT_EQ(7, mgr.limit().cost_size_);
        CASE_EXPECT_EQ(1, mgr.limit().cost_number_);

        res = mgr.front(pointer, s);
        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, res);
        CASE_EXPECT_EQ(pointer, atbus::detail::fn::buffer_step(check_ptr[2], 10));
        CASE_EXPECT_EQ(7, s);
        CASE_EXPECT_EQ(*reinterpret_cast<char*>(pointer), -1);

        // pop all
        res = mgr.pop(10);
        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, res);
        CASE_EXPECT_TRUE(mgr.empty());
        CASE_EXPECT_EQ(0, mgr.limit().cost_size_);
        CASE_EXPECT_EQ(0, mgr.limit().cost_number_);

        // pop nothing
        res = mgr.front(pointer, s);
        CASE_EXPECT_EQ(EN_ATBUS_ERR_NO_DATA, res);
        CASE_EXPECT_EQ(NULL, pointer);
        CASE_EXPECT_EQ(0, s);

        res = mgr.pop(10);
        CASE_EXPECT_EQ(EN_ATBUS_ERR_NO_DATA, res);
    }
}



CASE_TEST(buffer, static_buffer_manager_circle)
{
    atbus::detail::buffer_manager mgr;
    CASE_EXPECT_TRUE(mgr.empty());

    mgr.set_mode(1023, 10);
    CASE_EXPECT_FALSE(mgr.set_limit(2048, 10));

    CASE_EXPECT_EQ(1023, mgr.limit().limit_size_);
    CASE_EXPECT_EQ(10, mgr.limit().limit_number_);

    // size limit
    void* pointer;
    void* check_ptr[4];
    size_t s;
    int res = mgr.push(pointer, 256 - atbus::detail::buffer_block::head_size(256));
    CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, res);
    CASE_EXPECT_NE(NULL, pointer);
    res = mgr.push(pointer, 256 - atbus::detail::buffer_block::head_size(256));
    CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, res);
    CASE_EXPECT_NE(NULL, pointer);
    res = mgr.push(pointer, 256 - atbus::detail::buffer_block::head_size(256));
    CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, res);
    CASE_EXPECT_NE(NULL, pointer);
    res = mgr.push(pointer, 256 + 2 * atbus::detail::buffer_block::head_size(256));
    CASE_EXPECT_EQ(EN_ATBUS_ERR_BUFF_LIMIT, res);
    CASE_EXPECT_EQ(NULL, pointer);
    res = mgr.push(pointer, 257 - atbus::detail::buffer_block::head_size(257));
    CASE_EXPECT_EQ(EN_ATBUS_ERR_BUFF_LIMIT, res);
    CASE_EXPECT_EQ(NULL, pointer);
    res = mgr.push(pointer, 255 - atbus::detail::buffer_block::head_size(255));
    CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, res);
    CASE_EXPECT_NE(NULL, pointer);

    CASE_EXPECT_EQ(1023, mgr.limit().cost_size_ + atbus::detail::buffer_block::head_size(255) + 3 * atbus::detail::buffer_block::head_size(256));
    CASE_EXPECT_EQ(4, mgr.limit().cost_number_);


    mgr.pop(256);
    mgr.front(check_ptr[0], s);
    CASE_EXPECT_EQ(3, mgr.limit().cost_number_);

    res = mgr.push(pointer, 256);
    CASE_EXPECT_EQ(EN_ATBUS_ERR_BUFF_LIMIT, res);


    mgr.pop(255 - atbus::detail::buffer_block::head_size(256));
    CASE_EXPECT_EQ(3, mgr.limit().cost_number_);

    res = mgr.push(check_ptr[2], 128);
    CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, res);
    CASE_EXPECT_EQ(4, mgr.limit().cost_number_);

    

    res = mgr.push(check_ptr[3], 128);
    CASE_EXPECT_EQ(EN_ATBUS_ERR_BUFF_LIMIT, res);

    mgr.pop(1);
    CASE_EXPECT_EQ(3, mgr.limit().cost_number_);

    res = mgr.push(check_ptr[3], 384 - atbus::detail::buffer_block::head_size(100) - atbus::detail::buffer_block::head_size(412));
    CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, res);
    CASE_EXPECT_EQ(4, mgr.limit().cost_number_);


    mgr.front(check_ptr[1], s);
    CASE_EXPECT_TRUE(check_ptr[2] < check_ptr[0]);
    CASE_EXPECT_TRUE(check_ptr[3] <= check_ptr[0]);
    CASE_EXPECT_TRUE(check_ptr[3] > check_ptr[2]);
    CASE_EXPECT_TRUE(check_ptr[1] > check_ptr[0]);
}

