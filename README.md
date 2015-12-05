libatbus
========

用于搭建高性能、全异步(a)、树形结构(t)的BUS消息系统的跨平台框架库

依赖
------
工具依赖： 支持c++0x或c++11的编译器、cmake、msgpack（用于协议打解包）、libuv（用于网络通道）

> 如果本地未安装libuv，请clone 3rd_party/libuv 子模块


Why not c?
------
本来像用纯c写这个组件的，因为纯c比较容易控制结构清晰和代码简洁，但是为什么之后又改用C++了呢？首先一个原因是初期准备使用的协议打解包组件msgpack是c++的；另一方面c++提供了比较简单好用的数据结构容器和内存管理组件，更重要的是这些东西都是原生跨平台的。所以就干脆用C++和C++风格来实现。

*PS:GCC都转依赖C++了我为嘛不能用？*

关于MsgPack
------
本来是想用flatbuffer做协议打解包的，因为使用flatbuffer的话不需要额外引入外部库。
但是实际使用过程中发现flatbuffer使用上问题有点多（比如有地址对齐问题，动态变更数据有缓冲区限制，string类型预分配了过多缓冲区等等）。

相比之下msgpack兼容性，使用容易程度都好很多。另外虽然我没有针对性的测试，但是据说msgpack的性能大约是protobuf的4倍。
而且如果支持c++03/c++11的话也可以使用纯header库，这是一大优势。当然它由于不像protobuf那样对整数进行压缩，所以相对而言包体会大一些。
但是考虑到我们这个lib的实际作用是转发消息，包头和消息体相比非常的小。所以这部分protobuf的空间节约地非常有限。所以相对而言，CPU消耗和平台移植性显得更重要一些。


注意
------
为了代码尽量简洁（特别是少做无意义的平台兼容），依赖部分 C11和C++11的功能，所以不支持过低版本的编译器：
+ GCC: 4.4 及以上（建议gcc 4.8.1及以上）
+ Clang: 3.0 及以上 （建议 clang 3.4及以上）
+ VC: 10 及以上 （建议VC 12及以上）


支持
------
Linux下 GCC编译安装脚本(支持离线编译安装):

1. [GCC 5.X.X](https://github.com/owent-utils/bash-shell/tree/master/GCC%20Installer/gcc-5)
2. [GCC 4.9.X](https://github.com/owent-utils/bash-shell/tree/master/GCC%20Installer/gcc-4.9)
3. [LLVM & Clang](https://github.com/owent-utils/bash-shell/tree/master/LLVM%26Clang%20Installer)

LICENSE
------
libatbus 采用[MIT License](LICENSE)
MsgPack 采用[Boost Software License, Version 1.0协议](BOOST_LICENSE_1_0.txt)（类似MIT License）
libuv 采用[Node's license协议](NODE_S_LICENSE)（类似MIT License）