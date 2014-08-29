

typedef enum {
    EN_ATBUS_ERR_SUCCESS                    = 0,

    EN_ATBUS_ERR_PARAMS                     = -1,
    EN_ATBUS_ERR_INNER                      = -2,
    EN_ATBUS_ERR_NO_DATA                    = -3, // 无数据
    EN_ATBUS_ERR_BUFF_LIMIT                 = -4, // 缓冲区不足

    EN_ATBUS_ERR_CHANNEL_SIZE_TOO_SMALL     = -101,

    EN_ATBUS_ERR_NODE_BAD_BLOCK_FAST_CHECK  = -201,// 发现写坏的数据块 - 数据校验不通过
    EN_ATBUS_ERR_NODE_BAD_BLOCK_NODE_NUM    = -202,// 发现写坏的数据块 - 节点数量错误
    EN_ATBUS_ERR_NODE_BAD_BLOCK_BUFF_SIZE   = -203,// 发现写坏的数据块 - 节点数量错误
    EN_ATBUS_ERR_NODE_BAD_BLOCK_WSEQ_ID     = -204,// 发现写坏的数据块 - 写操作序列错误
    EN_ATBUS_ERR_NODE_BAD_BLOCK_CSEQ_ID     = -205,// 发现写坏的数据块 - 检查操作序列错误

    EN_ATBUS_ERR_NODE_TIMEOUT               = -211,// 操作超时


    EN_ATBUS_ERR_SHM_GET_FAILED             = -301,// 连接共享内存出错，具体错误原因可以查看errno或类似的位置

} ATBUS_ERROR_TYPE;

