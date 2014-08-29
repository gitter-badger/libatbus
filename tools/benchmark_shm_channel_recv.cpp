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

    int res = shm_init(shm_key, buffer_len, &channel, NULL);
    if (res < 0) {
        fprintf(stderr, "shm_init failed, ret: %d\n", res);
        return res;
    }

    srand(time(NULL));

    size_t sum_recv_len = 0;
    size_t sum_recv_times = 0;
    size_t sum_recv_err = 0;
    size_t sum_data_err = 0;

    // 创建读线程
    std::thread* read_threads;
    read_threads = new std::thread([&]{
        size_t buf_pool[1024];

        while(true) {
            size_t n = 0; // 最大 4K-8K的包

            int res = shm_recv(channel, buf_pool, sizeof(buf_pool), &n);
            n /= sizeof(size_t);

            if (res) {
                if (EN_ATBUS_ERR_NO_DATA != res) {
                    ++ sum_recv_err;
                } else {
                    std::this_thread::sleep_for(std::chrono::milliseconds(128));
                }
            } else {
                ++ sum_recv_times;
                sum_recv_len += n * sizeof(size_t);
                for (size_t i = 1; i < n; ++ i) {
                    if(buf_pool[0] != buf_pool[i]) {
                        ++ sum_data_err;
                        break;
                    }
                }
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
            "[ RUNNING  ] recv("<< sum_recv_times << " times, "<< sum_recv_len << " Bytes) "<<
            "recv err "<< sum_recv_err<< " times, data valid failed "<< sum_data_err<< " times"<<
            std::endl<< std::endl;
    }


    read_threads->join();
    delete read_threads;
    delete []buffer;
}
