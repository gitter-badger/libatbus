TODO
======
1. libuv 连接失败后是否需要uv_close?
2. uv_close 是否对 uv_stream_t 有效?
3. 边界条件测试
4. req和buf指针和callback核对
5. fd生效时序问题核对
6. 小缓冲区读+小缓冲区读完还有剩余（剩余不足以解包vint和足够解包vint）
7. 大缓冲区一次性读
8. 大缓冲区分批次读
9. 大缓冲区满+小缓冲区满
10. 异步数据的priv_data校验