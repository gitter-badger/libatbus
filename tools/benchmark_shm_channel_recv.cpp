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
        printf("usage: %s <shm key> [max unit size] [shm size]\n", argv[0]);
        return 0;
    }

    using namespace atbus::channel;
    size_t max_n = 1024;
    if (argc > 2)
        max_n = (size_t)strtol(argv[2], NULL, 10) / sizeof(size_t);

    size_t buffer_len = 64 * 1024 * 1024; // 64MB
    if (argc > 3)
        buffer_len = (size_t)strtol(argv[3], NULL, 10);

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
        size_t* buf_pool = new size_t[max_n];
        std::map<size_t, int> val_check;

        while(true) {
            size_t n = 0; // 最大 4K-8K的包

            int res = shm_recv(channel, buf_pool, sizeof(buf_pool), &n);
            n /= sizeof(size_t);

            if (res) {
                if (EN_ATBUS_ERR_NO_DATA != res) {
                    std::pair<size_t, size_t> last_action = shm_last_action();
                    fprintf(stderr, "shm_recv error, ret code: %d. start: %d, end: %d\n",
                        res, (int)last_action.first, (int)last_action.second
                    );
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

                std::map<size_t, int>::iterator iter = val_check.find(buf_pool[0]);
                if (val_check.end() == iter) {
                    std::cerr<< "new data index "<< buf_pool[0]<< std::endl;
                    std::cerr<< "all data index: ";
                    for (std::map<size_t, int>::iterator i = val_check.begin(); i != val_check.end(); ++ i){
                        std::cerr<< i->first<< ":"<< i->second<< ", ";
                    }
                    std::cerr<< std::endl;
                } else {
                    -- iter->second;
                    if (iter->second <= 0)
                        val_check.erase(iter);
                }

                iter = val_check.find(buf_pool[0] + 1);
                if (val_check.end() == iter)
                    val_check[buf_pool[0] + 1] = 1;
                else
                    ++ iter->second;
            }
        }

        delete []buf_pool;
    });


    // 检查状态
    int secs = 0;
    char unit_desc[][4] = {"B", "KB", "MB", "GB", "TB"};
    size_t unit_devi[] = {1ULL, 1ULL<< 10, 1ULL<< 20, 1ULL<< 30, 1ULL<< 40};
    size_t unit_index = 0;

    while (true) {
        ++ secs;
        std::chrono::seconds dura( 60 );
        std::this_thread::sleep_for( dura );

        while ( sum_recv_len / unit_devi[unit_index] > 1024 && unit_index < sizeof(unit_devi) / sizeof(size_t) - 1)
            ++ unit_index;

        while ( sum_recv_len / unit_devi[unit_index] <= 1024 && unit_index > 0)
            -- unit_index;

        std::cout<< "[ RUNNING  ] NO."<< secs<< " m"<< std::endl<<
            "[ RUNNING  ] recv("<< sum_recv_times << " times, "<< (sum_recv_len / unit_devi[unit_index]) << " "<< unit_desc[unit_index]<< ") "<<
            "recv err "<< sum_recv_err<< " times, data valid failed "<< sum_data_err<< " times"<<
            std::endl<< std::endl;

        shm_show_channel(channel, std::cout, false, 0);
    }


    read_threads->join();
    delete read_threads;
    delete []buffer;
}

