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


int main(int argc, char* argv[])
{
    if (argc < 2) {
        printf("usage: %s <shm key> [shm size]\n", argv[0]);
        return 0;
    }

    using namespace atbus::channel;
    size_t buffer_len = 64 * 1024 * 1024; // 64MB
    if (argc > 2)
        buffer_len = (size_t)strtol(argv[2], NULL, 10);
    char* buffer = new char[buffer_len];

    shm_channel* channel = NULL;
    key_t shm_key = (key_t)strtol(argv[1], NULL, 10);

    int res = shm_attach(shm_key, buffer_len, &channel, NULL);
    if (res < 0) {
        fprintf(stderr, "shm_attach failed, ret: %d\n", res);
        return res;
    }

    srand(time(NULL));

    size_t sum_send_len = 0;
    size_t sum_send_times = 0;
    size_t sum_send_full = 0;
    size_t sum_send_err = 0;
    size_t sum_seq = ((size_t)random() << 32);

    // 创建写线程
    std::thread* write_threads;
    write_threads = new std::thread([&]{
        size_t buf_pool[1024];

        while(true) {
            size_t n = rand() % 1024; // 最大 4K-8K的包
            ++ sum_seq;

            for (size_t i = 0; i < n; ++ i) {
                buf_pool[i] = sum_seq;
            }

            int res = shm_send(channel, buf_pool, n * sizeof(size_t));

            if (res) {
                if (EN_ATBUS_ERR_BUFF_LIMIT == res) {
                    ++ sum_send_full;
                } else {
                    ++ sum_send_err;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(32));
            } else {
                ++ sum_send_times;
                sum_send_len += n * sizeof(size_t);
            }
        }
    });


    // 检查状态
    int secs = 0;
    while (true) {
        ++ secs;
        std::chrono::seconds dura( 60 );
        std::this_thread::sleep_for( dura );
        std::cout<< "[ RUNNING  ] NO."<< secs<< " second(s)"<< std::endl<<
            "[ RUNNING  ] send("<< sum_send_times << " times, "<< sum_send_len << " Bytes) "<<
            "full "<< sum_send_full<< " times, err "<< sum_send_err<< " times"<<
            std::endl<< std::endl;
    }


    write_threads->join();
    delete write_threads;
    delete []buffer;
}
