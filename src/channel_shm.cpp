/**
 * @brief 所有channel文件的模式均为 c + channel<br />
 *        使用c的模式是为了简单、结构清晰并且避免异常<br />
 *        附带c++的部分是为了避免命名空间污染并且c++的跨平台适配更加简单
 */

#include <cstdio>
#include <assert.h>
#include <ctime>
#include <stdint.h>
#include <cstddef>
#include <cstring>
#include <cstdlib>
#include <atomic>

#include <detail/libatbus_error.h>

#include "channel_export.h"

#ifdef __unix__
#include <unistd.h>
#endif

namespace atbus {
    namespace channel {

        struct shm_channel {
        };

        struct shm_conf {
        };

        typedef union {
            shm_channel* shm;
            mem_channel* mem;
        } shm_channel_switcher;

        typedef union {
            const shm_conf* shm;
            const mem_conf* mem;
        } shm_conf_cswitcher;

        static int shm_get_buffer(key_t shm_key, size_t len, void** data, size_t* real_size, bool create) {
        #ifdef __unix__
            // len 长度对齐到分页大小
            size_t page_size = ::sysconf(_SC_PAGESIZE);
            len = (len + page_size - 1) & (~(page_size - 1));

            int shmflag = 0666;
            if (create)
                shmflag |= IPC_CREAT;

        #ifdef __linux__
            // linux下阻止从交换分区分配物理页
            shmflag |= SHM_NORESERVE;

        #ifdef ATBUS_MACRO_HUGETLB_SIZE
            // 如果大于4倍的大页表，则对齐到大页表并使用大页表
            if (len > (4 * ATBUS_MACRO_HUGETLB_SIZE)) {
                len = (len + (ATBUS_MACRO_HUGETLB_SIZE) - 1) & (~((ATBUS_MACRO_HUGETLB_SIZE) - 1));
                shmflag |= SHM_HUGETLB;
            }
        #endif

        #endif
            int shm_id = shmget(shm_key, len, shmflag);
            if (-1 == shm_id)
                return EN_ATBUS_ERR_SHM_GET_FAILED;

            // 获取实际长度
            if (real_size) {
                struct shmid_ds shm_info;
                if(shmctl(shm_id, IPC_STAT, &shm_info))
                    return EN_ATBUS_ERR_SHM_GET_FAILED;

                *real_size = shm_info.shm_segsz;
            }

            // 获取地址
            if(data)
                *data = shmat(shm_id, NULL, 0);

        #else
        #endif

            return EN_ATBUS_ERR_SUCCESS;
        }

        int shm_attach(key_t shm_key, size_t len, shm_channel** channel, const shm_conf* conf) {
            shm_channel_switcher channel_s;
            shm_conf_cswitcher conf_s;
            conf_s.shm = conf;

            size_t real_size;
            void* buffer;
            int ret = shm_get_buffer(shm_key, len, &buffer, &real_size, false);
            if (ret < 0)
                return ret;

            ret = mem_attach(buffer, real_size, &channel_s.mem, conf_s.mem);
            if(ret < 0)
                return ret;

            if (channel)
                *channel = channel_s.shm;

            return ret;
        }

        int shm_init(key_t shm_key, size_t len, shm_channel** channel, const shm_conf* conf) {
            shm_channel_switcher channel_s;
            shm_conf_cswitcher conf_s;
            conf_s.shm = conf;

            size_t real_size;
            void* buffer;
            int ret = shm_get_buffer(shm_key, len, &buffer, &real_size, true);
            if (ret < 0)
                return ret;

            ret = mem_init(buffer, real_size, &channel_s.mem, conf_s.mem);
            if(ret < 0)
                return ret;

            if (channel)
                *channel = channel_s.shm;

            return ret;
        }

        int shm_send(shm_channel* channel, const void* buf, size_t len) {
            shm_channel_switcher switcher;
            switcher.shm = channel;
            return mem_send(switcher.mem, buf, len);
        }

        int shm_recv(shm_channel* channel, void* buf, size_t len, size_t* recv_size) {
            shm_channel_switcher switcher;
            switcher.shm = channel;
            return mem_recv(switcher.mem, buf, len, recv_size);
        }

        std::pair<size_t, size_t> shm_last_action() {
            return mem_last_action();
        }

        void shm_show_channel(shm_channel* channel, std::ostream& out, bool need_node_status, size_t need_node_data) {
            shm_channel_switcher switcher;
            switcher.shm = channel;
            mem_show_channel(switcher.mem, out, need_node_status, need_node_data);
        }
    }
}
