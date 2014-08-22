

typedef enum {
    EN_ATBUS_ERR_SUCCESS                    = 0,

    EN_ATBUS_ERR_PARAMS                     = -1,
    EN_ATBUS_ERR_INNER                      = -2,
    EN_ATBUS_ERR_NO_DATA                    = -3, // 无数据
    EN_ATBUS_ERR_BUFF_LIMIT                 = -4, // 缓冲区不足

    EN_ATBUS_ERR_CHANNEL_SIZE_TOO_SMALL     = -101,

    EN_ATBUS_ERR_NODE_BAD_BLOCK             = -201,// 发现写坏的数据块

    EN_ATBUS_ERR_SHM_GET_FAILED             = -301,// 发现写坏的数据块

} ATBUS_ERROR_TYPE;

