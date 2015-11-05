/**
 * libatbus.h
 *
 *  Created on: 2015年10月29日
 *      Author: owent
 */

#pragma once

#ifndef LIBATBUS_NODE_H_
#define LIBATBUS_NODE_H_

#include <bitset>
#include <ctime>
#include <list>

#include "std/smart_ptr.h"
#include "design_pattern/noncopyable.h"

#include "detail/libatbus_error.h"
#include "detail/libatbus_config.h"
#include "detail/libatbus_channel_export.h"

#include "atbus_proto_generated.h"

namespace atbus {
    class node: public util::design_pattern::noncopyable {
    public:
        typedef ATBUS_MACRO_BUSID_TYPE bus_id_t;
        typedef struct {
            enum type {
                EN_CONF_GLOBAL_ROUTER,                  /** 全局路由表 **/
                EN_CONF_MAX
            };
        } flag_t;
        typedef struct {
            adapter::loop_t* ev_loop;
            uint32_t children_mask;                     /** 子节点掩码 **/
            std::bitset<flag_t::EN_CONF_MAX> flags;     /** 开关配置 **/
            std::string father_address;                 /** 父节点地址 **/
            int loop_times;                             /** 消息循环次数限制，防止某些通道繁忙把其他通道堵死 **/

            // ===== 连接配置 =====
            int backlog;
            time_t  first_idle_timeout;                 /** 第一个包允许的空闲时间，毫秒 **/

            // ===== 缓冲区配置 =====
            size_t msg_size;                            /** 数据包大小 **/
            size_t recv_buffer_size;                    /** 接收缓冲区，和数据包大小有关 **/
            size_t send_buffer_size;                    /** 发送缓冲区限制 **/
            size_t send_buffer_number;                  /** 发送缓冲区静态Buffer数量限制，0则为动态缓冲区 **/
        } conf_t;

        // ================== 用这个来取代C++继承，减少学习成本 ==================
        struct no_stream_channel_t {
            void* channel;
            key_t key;
            int(*proc_fn)(node&, no_stream_channel_t*, time_t, time_t);
            int(*free_fn)(node&, no_stream_channel_t*);
        };
    public:
        static void default_conf(conf_t* conf);

    public:
        node();
        ~node();

        /**
         * @brief 数据初始化
         * @return 0或错误码
         */
        int init(bus_id_t id, const conf_t* conf);

        /**
         * @brief 数据重置（释放资源）
         * @return 0或错误码
         */
        int reset();

        /**
         * @brief 执行一帧
         * @param sec 当前时间-秒
         * @param sec 当前时间-微秒
         * @return 本帧处理的消息数
         */
        int proc(time_t sec, time_t usec);

        /**
         * @brief 监听数据接收地址
         * @param addr 监听地址
         * @return 0或错误码
         */
        int listen(const char* addr);

        /**
         * @brief 连接到目标地址
         * @param addr 连接目标地址
         * @return 0或错误码
         */
        //int connect(const char* addr);


        /**
         * @brief 监听数据接收地址
         * @param tid 发送目标ID
         * @param type 自定义类型，将作为msg.head.type字段传递。可用于业务区分服务类型
         * @param buffer 数据块地址
         * @param s 数据块长度
         * @return 0或错误码
         */
        //int send_to(bus_id_t tid, int type, const void* buffer, size_t s);

    private:
        adapter::loop_t* get_evloop(); 
        channel::io_stream_channel* get_iostream_channel();
        channel::io_stream_conf* get_iostream_conf();

        static void iostream_on_recv_cb(const channel::io_stream_channel* channel,const channel::io_stream_connection* connection,int status,void* buffer,size_t s);
        static void iostream_on_connected(const channel::io_stream_channel* channel, const channel::io_stream_connection* connection, int status, void* buffer, size_t s);
        static void iostream_on_disconnected(const channel::io_stream_channel* channel, const channel::io_stream_connection* connection, int status, void* buffer, size_t s);


        void on_recv(const protocol::msg* m, int status, int errcode);


        static int shm_proc_fn(node& n, no_stream_channel_t* c, time_t sec, time_t usec);

        static int shm_free_fn(node& n, no_stream_channel_t* c);

        static int mem_proc_fn(node& n, no_stream_channel_t* c, time_t sec, time_t usec);

        static int mem_free_fn(node& n, no_stream_channel_t* c);

    public:
        inline bus_id_t get_id() const { return id_; }
    private:
        // ============ 基础信息 ============
        // ID
        bus_id_t id_;
        // 配置
        conf_t conf_;

        // ============ IO事件数据 ============
        // 事件分发器
        adapter::loop_t* ev_loop_;
        std::shared_ptr<channel::io_stream_channel> iostream_channel_;
        std::unique_ptr<channel::io_stream_conf> iostream_conf_;

        // 轮训接收通道集
        detail::buffer_block* static_buffer_;
        std::list<no_stream_channel_t> basic_channels;

        // 基于事件的通道信息
        // 基于事件的通道超时收集


        // ============ 节点逻辑关系数据 ============
        // 兄弟节点

        // 子节点

        // 全局路由表

        // 统计信息
    };
}

#endif /* LIBATBUS_NODE_H_ */
