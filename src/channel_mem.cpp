/**
 * @brief 所有channel文件的模式均为 c + channel<br />
 *        使用c的模式是为了简单、结构清晰并且避免异常<br />
 *        附带c++的部分是为了避免命名空间污染并且c++的跨平台适配更加简单
 */

#include <assert.h>
#include <ctime>
#include <stdint.h>
#include <cstddef>
#include <cstring>
#include <cstdlib>
#include <atomic>

#include <detail/libatbus_error.h>

#ifndef ATBUS_MACRO_DATA_NODE_SIZE
#define ATBUS_MACRO_DATA_NODE_SIZE 128
#endif

#ifndef ATBUS_MACRO_DATA_ALIGN_TYPE
#define ATBUS_MACRO_DATA_ALIGN_TYPE size_t
#endif

namespace atbus {
    namespace channel {

        typedef ATBUS_MACRO_DATA_ALIGN_TYPE data_align_type;

        // 配置数据结构
        typedef struct {
            uint64_t conf_send_timeout_ms;
        } mem_conf;

        // 通道头
        typedef struct {
            // 数据节点
            size_t node_size;
            size_t node_count;

            // [atomic_read_cur, atomic_write_cur) 内的数据块都是已使用的数据块
            // atomic_write_cur指向的数据块一定是空块，故而必然有一个node的空洞
            // c11的stdatomic.h在很多编译器不支持并且还有些潜规则(gcc 不能使用-fno-builtin 和 -march=xxx)，故而使用c++版本
            volatile std::atomic<size_t> atomic_read_cur;   // std::atomic也是POD类型
            volatile std::atomic<size_t> atomic_write_cur;  // std::atomic也是POD类型

            // 配置
            mem_conf conf;
            volatile std::atomic<size_t> atomic_recver_identify; // 接收端校验号(用于保证只有一个接收者)

            // 统计信息
            size_t block_bad_count; // 读取到坏块次数
            size_t block_timeout_count; // 读取到写入超时块次数
        } mem_channel;

        // 对齐头
        typedef union {
            mem_channel channel;
            char align[4 * 1024]; // 对齐到4KB,用于以后拓展
        } mem_channel_head_align;


        // 数据节点头
        typedef struct {
            uint32_t flag;
        } mem_node_head;

        // 数据头
        typedef struct {
            mem_node_head head;
            size_t buffer_size;
            uint64_t first_read_time;
            data_align_type fast_check;
        } mem_block_head;


        typedef enum {
            MF_WRITEN       = 0x00000001,
            MF_START_NODE   = 0x00000002,
            MF_END_NODE     = 0x00000004,
        } MEM_FLAG;

        /**
         * @brief 检查标记位
         * @param flag 待操作的flag
         * @param checked 检查项
         * @return 检查结果flag
         */
        static inline uint32_t check_flag(uint32_t flag, MEM_FLAG checked) {
            return flag & checked;
        }

        /**
         * @brief 设置标记位
         * @param flag 待操作的flag
         * @param checked 设置项flag
         * @return 设置结果flag
         */
        static inline uint32_t set_flag(uint32_t flag, MEM_FLAG checked) {
            return flag | checked;
        }

        /**
         * @brief 生存默认配置
         * @param conf
         */
        static void mem_default_conf(mem_conf * conf) {
            assert(conf);

            conf->conf_send_timeout_ms = 1;
        }

        /**
         * @brief 内存通道常量
         */
        static struct mem_block {
            static const size_t channel_head_size = sizeof(mem_channel_head_align);
            static const size_t block_head_size = ((sizeof(mem_block_head) - 1) / sizeof(data_align_type) + 1) * sizeof(data_align_type);
            static const size_t node_head_size = ((sizeof(mem_node_head) - 1) / sizeof(data_align_type) + 1) * sizeof(data_align_type);
        };

        /**
         * @brief 检测数字是2的几次幂
         */
        template<size_t S>
        struct mem_bin_power_check {
            static_assert(0 == (S & (S - 1)), "not 2^N"); // 必须是2的N次幂
            static_assert(S, "must not be 0"); // 必须大于0

            enum {
                value = mem_bin_power_check< (S >> 1) >::value + 1
            };
        };

        template<>
        struct mem_bin_power_check<1> {
            enum {
                value = 0
            };
        };

        /**
         * @brief 获取数据节点head
         * @param channel 内存通道
         * @param index 节点索引
         * @param data 数据区起始地址
         * @param data_len 数据区长度
         * @return 节点head指针
         */
        static inline mem_node_head* mem_get_node_head(mem_channel* channel, size_t index, void** data, size_t* data_len) {
            assert(channel);
            assert(index < channel->node_count);

            char* buf = (char*)channel;
            buf += mem_block::channel_head_size;

            buf += index * channel->node_size;

            if (data)
                (*data) = (void*)(buf + mem_block::node_head_size);
            if (data_len)
                (*data_len) = channel->node_size - mem_block::node_head_size;

            return (mem_node_head*)buf;
        }

        /**
         * @brief 获取数据数据块head
         * @param channel 内存通道
         * @param index 节点索引
         * @return 数据块head指针
         */
        static inline mem_block_head* mem_get_block_head(mem_channel* channel, size_t index, void** data, size_t* data_len) {
            assert(channel);
            assert(index < channel->node_count);

            char* buf = (char*)channel;
            buf += mem_block::channel_head_size;

            buf += index * channel->node_size;

            if (data)
                (*data) = (void*)(buf + mem_block::block_head_size);
            if (data_len)
                (*data_len) = channel->node_size - mem_block::block_head_size;

            return (mem_block_head*)buf;
        }

        /**
         * @brief 利用系统总线做快速copy
         * @param dest 拷贝目标
         * @param src 拷贝源数据
         * @param len 拷贝长度
         * @param check 校验数据
         * @note 大数据拷贝：VC下性能大约是原生memcpy的5倍，GCC下大约是原生memcpy性能的4倍，Clang下大约是原生memcpy性能的1.5倍
         * @note 小数据拷贝：VC下性能大约是原生memcpy的5倍，GCC下大约是原生memcpy性能的2倍，Clang下大约是原生memcpy性能的1.4倍
         * @note 以上数据测试都是在开启编译优化的情况下(编译器自带lib都是开了编译优化的)
         */
        static inline void fast_memcpy(void* dest, const void* src, size_t len, data_align_type* check) {
            data_align_type* adest = (data_align_type*) dest;
            const data_align_type* asrc = (const data_align_type*) src;

            size_t loop = len >> mem_bin_power_check<sizeof(data_align_type)>::value;
            while (loop --) {
                *adest = *asrc;

                // 校验码算法
                if (check) {
                    *check ^= *asrc;
                }

                // 这里使用自增以便于编译器优化
                ++ adest;
                ++ asrc;
            }

            // 未对齐部分直接memcpy
            loop = len & (sizeof(data_align_type) - 1);
            dest = adest;
            src = asrc;
            while (loop --) {
                *(char*)dest = (*(char*)src);
                dest = ((char*)dest + 1);
                src = ((char*)src + 1);
            }
        }

        // 节点大小一定要大于头结点
        static_assert(ATBUS_MACRO_DATA_NODE_SIZE > mem_block::block_head_size, "node size must be greater than block head");
        // 对齐单位的大小必须是2的N次方
        static_assert(0 == (sizeof(data_align_type) & (sizeof(data_align_type) - 1)), "data align size must be 2^N");
        // 节点大小必须是对齐单位的2的N次方倍
        static_assert(0 == (ATBUS_MACRO_DATA_NODE_SIZE & (ATBUS_MACRO_DATA_NODE_SIZE - data_align_type)), "node size must be [data align size] * 2^N");

        int mem_init(void* buf, size_t len, mem_channel** channel, const mem_conf* conf) {
            // 缓冲区最小长度为数据头+空洞node的长度
            if (len < sizeof(mem_channel_head_align) + ATBUS_MACRO_DATA_NODE_SIZE)
                return EN_ATBUS_ERR_CHANNEL_SIZE_TOO_SMALL;

            memset(buf, 0x00, len);
            mem_channel_head_align* head = (mem_channel_head_align*)buf;

            head->channel.node_size = ATBUS_MACRO_DATA_NODE_SIZE;
            head->channel.node_count = (len - mem_block::channel_head_size) / head->channel.node_size;

            if (NULL != conf)
                memcpy(&head->channel.conf, &conf, sizeof(conf));
            else
                mem_default_conf(&head->channel.conf);

            if (channel)
                *channel = &head->channel;

            return EN_ATBUS_ERR_SUCCESS;
        }

        int mem_send(mem_channel* channel, const void* buf, size_t len) {
            if (NULL == channel)
                return EN_ATBUS_ERR_PARAMS;

            size_t head_data_size = channel->node_size - mem_block::block_head_size;
            size_t node_count = 1;
            if (len > head_data_size) {
                size_t node_data_size = channel->node_size - mem_block::node_head_size;
                node_count += (len - head_data_size - 1) / node_data_size + 1;
            }

            // 要写入的数据比可用的缓冲区还大
            if (node_count >= channel->node_count)
                return EN_ATBUS_ERR_BUFF_LIMIT;

            size_t read_cur = 0;
            size_t new_write_cur, write_cur = std::atomic_load(&channel->atomic_write_cur);

            while(true) {
                read_cur = std::atomic_load(&channel->atomic_read_cur);

                size_t available_node = (read_cur + channel->node_count - write_cur) % channel->node_count;
                // 要留下一个node做tail
                if (node_count >= available_node)
                    return EN_ATBUS_ERR_BUFF_LIMIT;

                // 新的尾部node游标
                new_write_cur = (write_cur + node_count) % channel->node_count;

                // CAS
                bool f = std::atomic_compare_exchange_weak(&channel->atomic_write_cur, &write_cur, new_write_cur);

                if (f)
                    break;

                // 发现冲突原子操作失败则重试
            }

            // 数据写入.头节点数据写入
            //fast_memcpy
            void* buffer_start = NULL;
            size_t buffer_len = 0;
            mem_block_head* block_head = mem_get_block_head(channel, write_cur, &buffer_start, &buffer_len);
            block_head->head.flag = set_flag(block_head->head.flag, MF_START_NODE);

            // 头结点足够大
            if (len <= buffer_len) {
                fast_memcpy(buffer_start, buf, len, &block_head->fast_check);
                block_head->head.flag = set_flag(block_head->head.flag, MF_END_NODE);
                block_head->head.flag = set_flag(block_head->head.flag, MF_WRITEN);
                return EN_ATBUS_ERR_SUCCESS;
            }

            // 头节点不够大
            fast_memcpy(buffer_start, buf, buffer_len, &block_head->fast_check);
            len -= buffer_len;
            buf = (void*)((char*)buf + buffer_len);
            ++ write_cur;

            // 数据写入.数据节点数据写入
            while (len > 0) {
                // 不应该到达空洞位置
                if (write_cur == new_write_cur)
                    return EN_ATBUS_ERR_INNER;

                mem_node_head* node_head = mem_get_node_head(channel, write_cur, &buffer_start, &buffer_len);
                if (len <= buffer_len) {
                    fast_memcpy(buffer_start, buf, len, &block_head->fast_check);
                    node_head->flag = set_flag(node_head->flag, MF_END_NODE);
                    node_head->flag = set_flag(node_head->flag, MF_WRITEN);
                    break;
                }

                // TODO ...
                ++ write_cur;
            }

            // 头节点设为可用
            block_head->head.flag = set_flag(block_head->head.flag, MF_WRITEN);
            return EN_ATBUS_ERR_SUCCESS;
        }

        int mem_recv(mem_channel* channel, void* buf, size_t len) {
            if (NULL == channel)
                return EN_ATBUS_ERR_PARAMS;

            while(true) {
                size_t read_cur = std::atomic_load(&channel->atomic_read_cur);
                size_t write_cur = std::atomic_load(&channel->atomic_write_cur);

                if (read_cur == write_cur)
                    return EN_ATBUS_ERR_NO_DATA;
            }

            return EN_ATBUS_ERR_SUCCESS;
        }

    }
}
