#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <map>
#include <atomic>
#include <memory>
#include <thread>
#include <chrono>
#include <functional>

#include <detail/libatbus_error.h>
#include "channel_export.h"
#include "frame/test_macros.h"


CASE_TEST(channel, mem_siso)
{
    using namespace atbus::channel;
    const size_t buffer_len = 512 * 1024 * 1024; // 512MB
    char* buffer = new char[buffer_len];

    mem_channel* channel = NULL;

    CASE_EXPECT_EQ(0, mem_init(buffer, buffer_len, &channel, NULL));
    CASE_EXPECT_NE(NULL, channel);
    // 4KB header

    // 数据初始化
    char buf_group1[2][32] = {0};
    char buf_group2[2][45] = {0};
    char buf_group3[2][133] = {0};
    char buf_group4[2][605] = {0};
    size_t len_group[] = {32, 45, 133, 605};
    size_t group_num = sizeof(len_group) / sizeof(size_t);
    char* buf_group[] = {buf_group1[0], buf_group2[0], buf_group3[0], buf_group4[0]};
    char* buf_rgroup[] = {buf_group1[1], buf_group2[1], buf_group3[1], buf_group4[1]};

    {
        size_t i = 0;
        char j = 1;

        for(; i < group_num; ++ i, ++j)
            for (size_t k = 0; k < len_group[i]; ++ k)
                buf_group[i][k] = j;
    }

    size_t send_sum_len;
    // 单进程写压力
    {
        size_t sum_len = 0, times = 0;
        int res = 0;
        size_t i = 0;
        clock_t bt = clock();
        while (0 == res) {
            res = mem_send(channel, buf_group[i], len_group[i]);
            if (!res) {
                sum_len += len_group[i];
                ++ times;
            }

            i = (i + 1) % group_num;
        }
        clock_t et = clock();

        CASE_EXPECT_EQ(EN_ATBUS_ERR_BUFF_LIMIT, res);

        std::cout<< "[ RUNNING  ] send "<< sum_len<< " bytes("<< times<< " times) in "<< ((et - bt) / (CLOCKS_PER_SEC / 1000))<< "ms"<< std::endl;
        send_sum_len = sum_len;
    }

    size_t recv_sum_len;
    // 单进程读压力
    {
        size_t sum_len = 0, times = 0;
        int res = 0;
        size_t i = 0;
        clock_t bt = clock();
        while (0 == res) {
            size_t len;
            res = mem_recv(channel, buf_rgroup[i], len_group[i], &len);
            if (0 == res) {
                CASE_EXPECT_EQ(len, len_group[i]);
                sum_len += len_group[i];
                ++ times;
            }

            i = (i + 1) % group_num;
        }
        clock_t et = clock();

        CASE_EXPECT_EQ(EN_ATBUS_ERR_NO_DATA, res);
        std::cout<< "[ RUNNING  ] recv "<< sum_len<< " bytes("<< times<< " times) in "<< ((et - bt) / (CLOCKS_PER_SEC / 1000))<< "ms"<< std::endl;
        recv_sum_len = sum_len;
    }

    // 简单数据校验
    {
        for (size_t i = 0; i < group_num; ++ i) {
            CASE_EXPECT_EQ(0, memcmp(buf_group[i], buf_rgroup[i], len_group[i]));
        }
    }

    CASE_EXPECT_EQ(recv_sum_len, send_sum_len);

    delete []buffer;
}


CASE_TEST(channel, mem_miso)
{
    using namespace atbus::channel;
    const size_t buffer_len = 64 * 1024 * 1024; // 64MB
    char* buffer = new char[buffer_len];

    mem_channel* channel = NULL;

    CASE_EXPECT_EQ(0, mem_init(buffer, buffer_len, &channel, NULL));
    CASE_EXPECT_NE(NULL, channel);
    // 4KB header

    srand(time(NULL));

    int left_sec = 16;
    std::atomic<size_t> sum_send_len;
    sum_send_len.store(0);
    std::atomic<size_t> sum_send_times;
    sum_send_times.store(0);
    std::atomic<size_t> sum_send_full;
    sum_send_full.store(0);
    std::atomic<size_t> sum_send_err;
    sum_send_err.store(0);
    std::atomic<size_t> sum_seq;
    sum_seq.store(0);
    size_t sum_recv_len = 0;
    size_t sum_recv_times = 0;
    size_t sum_recv_err = 0;

    // 创建四个写线程
    const size_t wn = 4;
    std::thread* write_threads[wn];
    for (size_t i = 0; i < wn; ++ i) {
        write_threads[i] = new std::thread([&]{
            size_t buf_pool[1024];

            while(left_sec > 0) {
                size_t n = rand() % 1024; // 最大 4K-8K的包
                //size_t n = 16;
                size_t seq = sum_seq.fetch_add(1);
                for (size_t i = 0; i < n; ++ i) {
                    buf_pool[i] = seq;
                }

                int res = mem_send(channel, buf_pool, n * sizeof(size_t));

                if (res) {
                    if (EN_ATBUS_ERR_BUFF_LIMIT == res) {
                        ++ sum_send_full;
                    } else {
                        ++ sum_send_err;
                    }
                    std::this_thread::yield();
                } else {
                    ++ sum_send_times;
                    sum_send_len += n * sizeof(size_t);
                }
            }
        });
    }

    // 读进程
    std::chrono::milliseconds dura( 200 );
    std::thread* read_thread = new std::thread([&]{
        size_t buff_recv[1024]; // 最大 4K-8K的包
        int read_failcount = 10;

        while(read_failcount >= 0) {
            size_t len = 0;
            // std::this_thread::sleep_for( std::chrono::milliseconds( 1000 ) );
            int res = mem_recv(channel, buff_recv, sizeof(buff_recv), &len);
            if (res) {
                if (EN_ATBUS_ERR_NO_DATA == res) {
                    std::this_thread::yield();
                    -- read_failcount;
                } else {
                    CASE_EXPECT_EQ(EN_ATBUS_ERR_NODE_BAD_BLOCK, res);
                    ++ sum_recv_err;
                }
            } else {
                sum_recv_len += len;
                ++ sum_recv_times;

                CASE_EXPECT_EQ(0, len % sizeof(size_t));
                len /= sizeof(size_t);


                for (size_t i = 1; i < len; ++ i) {
                    bool flag = buff_recv[i] == buff_recv[0];
                    CASE_EXPECT_EQ(buff_recv[i], buff_recv[0]);
                    if (!flag) {
                        break;
                    }
                }

                read_failcount = 10;
            }
        }
    });


    // 检查状态
    {
        int secs = 0;
        do {
            -- left_sec;
            ++ secs;
            std::chrono::milliseconds dura( 1000 );
            std::this_thread::sleep_for( dura );
            std::cout<< "[ RUNNING 1] NO."<< secs<< " second(s)"<< std::endl<<
                "[ RUNNING 2] recv("<< sum_recv_times<< " times, "<< sum_recv_len << " Bytes) err "<<
                sum_recv_err<< " times"<< std::endl<<
                "[ RUNNING 3] send("<< sum_send_times << " times, "<< sum_send_len << " Bytes) "<<
                "full "<< sum_send_times<< " times, err "<< sum_send_err<< " times"<< std::endl;

        } while (left_sec >= 0);
    }


    for (size_t i = 0; i < wn; ++ i) {
        write_threads[i]->join();
        delete write_threads[i];
    }

    read_thread->join();
    delete read_thread;
    delete []buffer;
}
