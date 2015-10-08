#pragma once

#ifndef LIBATBUS_DETAIL_LIBATBUS_ERROR_H_
#define LIBATBUS_DETAIL_LIBATBUS_ERROR_H_

typedef enum {
    EN_ATBUS_ERR_SUCCESS                    = 0,

    EN_ATBUS_ERR_PARAMS                     = -1,
    EN_ATBUS_ERR_INNER                      = -2,
    EN_ATBUS_ERR_NO_DATA                    = -3, // 无数据
    EN_ATBUS_ERR_BUFF_LIMIT                 = -4, // 缓冲区不足
    EN_ATBUS_ERR_MALLOC                     = -5, // 分配失败
    EN_ATBUS_ERR_SCHEME                     = -6, // 协议错误
    EN_ATBUS_ERR_EOF                        = -7, // 数据流终止

    EN_ATBUS_ERR_CHANNEL_SIZE_TOO_SMALL     = -101,

    EN_ATBUS_ERR_NODE_BAD_BLOCK_FAST_CHECK  = -201,// 发现写坏的数据块 - 数据校验不通过
    EN_ATBUS_ERR_NODE_BAD_BLOCK_NODE_NUM    = -202,// 发现写坏的数据块 - 节点数量错误
    EN_ATBUS_ERR_NODE_BAD_BLOCK_BUFF_SIZE   = -203,// 发现写坏的数据块 - 节点数量错误
    EN_ATBUS_ERR_NODE_BAD_BLOCK_WSEQ_ID     = -204,// 发现写坏的数据块 - 写操作序列错误
    EN_ATBUS_ERR_NODE_BAD_BLOCK_CSEQ_ID     = -205,// 发现写坏的数据块 - 检查操作序列错误

    EN_ATBUS_ERR_NODE_TIMEOUT               = -211,// 操作超时


    EN_ATBUS_ERR_SHM_GET_FAILED             = -301,// 连接共享内存出错，具体错误原因可以查看errno或类似的位置
    EN_ATBUS_ERR_SHM_NOT_FOUND              = -302,// 共享内存未找到

    EN_ATBUS_ERR_SOCK_BIND_FAILED           = -401,// 绑定地址或端口失败
    EN_ATBUS_ERR_SOCK_LISTEN_FAILED         = -402,// 监听失败
    EN_ATBUS_ERR_SOCK_CONNECT_FAILED        = -403,// 连接失败

    EN_ATBUS_ERR_PIPE_BIND_FAILED           = -501,// 绑定地址或端口失败
    EN_ATBUS_ERR_PIPE_LISTEN_FAILED         = -502,// 监听失败
    EN_ATBUS_ERR_PIPE_CONNECT_FAILED        = -503,// 连接失败

    EN_ATBUS_ERR_DNS_GETADDR_FAILED         = -601,// DNS解析失败
    EN_ATBUS_ERR_CONNECTION_NOT_FOUND       = -602,// 找不到连接
    EN_ATBUS_ERR_WRITE_FAILED               = -603,// 底层API写失败
    EN_ATBUS_ERR_READ_FAILED                = -604,// 底层API读失败
} ATBUS_ERROR_TYPE;

#endif
