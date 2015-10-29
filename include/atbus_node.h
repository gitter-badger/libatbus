/**
 * libatbus.h
 *
 *  Created on: 2015年10月29日
 *      Author: owent
 */

#pragma once

#ifndef LIBATBUS_NODE_H_
#define LIBATBUS_NODE_H_

#include "detail/libatbus_error.h"
#include "detail/libatbus_config.h"
#include "detail/libatbus_adapter_libuv.h"
#include "design_pattern/noncopyable.h"

namespace atbus {
    class node: public util::design_pattern::noncopyable {
    public:
        typedef ATBUS_MACRO_BUSID_TYPE bus_id_t;
        typedef struct {
            bool global_router;         /** 全局路由表 **/
            uint32_t children_mask;     /** 子节点掩码 **/
        } conf_t;

    public:
        node();
        ~node();

        /**
         * @brief 数据初始化
         * @return 0或错误码
         */
        //int init();

        /**
         * @brief 数据重置（释放资源）
         * @return 0或错误码
         */
        //int reset();

        /**
         * @brief 执行一帧
         * @return 本帧处理的消息数
         */
        //int proc();

        /**
         * @brief 监听数据接收地址
         * @param addr 监听地址
         * @return 0或错误码
         */
        //int listen(const char* addr);


        /**
         * @brief 监听数据接收地址
         * @param tid 发送目标ID
         * @param buffer 数据块地址
         * @param s 数据块长度
         * @return 0或错误码
         */
        //int send_to(bus_id_t tid, const void* buffer, size_t s);
    private:
        // ============ 基础信息 ============
        // ID
        bus_id_t id_;
        // 配置

        // ============ IO事件数据 ============
        // 事件分发器
        adapter::loop_t* ev_loop_;

        // 轮训接收通道集

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
