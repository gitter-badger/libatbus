libatbus
========

用于搭建高性能、全异步、树形结构的BUS消息系统的跨平台框架库

依赖
------
工具依赖： 支持c++0x或c++11的编译器、cmake、flatbuffers（用于协议打解包）


Why not c?
------
本来像用纯c写这个组件的，因为纯c比较容易控制结构清晰和代码简洁，但是为什么之后又改用C++了呢？首先一个原因是初期准备使用的协议打解包组件flatbuffers是c++的；另一方面c++提供了比较简单好用的数据结构容器和内存管理组件，更重要的是这些东西都是原生跨平台的。所以就干脆用C++和C++风格来实现。

*PS:GCC都转依赖C++了我为嘛不能用？*


注意
------
为了代码尽量简洁（特别是少做无意义的平台兼容），依赖部分 C11和C++11的功能，所以不支持过低版本的编译器：
+ GCC: 4.4 及以上（建议gcc 4.8.1及以上）
+ Clang: 3.0 及以上 （建议 clang 3.4及以上）
+ VC: 10 及以上 （建议VC 12及以上）


支持
------
Linux下 GCC编译安装脚本(支持离线编译安装):

1. [GCC 4.9.X](https://github.com/owt5008137/OWenT-s-Utils/tree/master/Bash%26Shell/GCC%20Installer/gcc-4.9)
2. [GCC 4.8.X](https://github.com/owt5008137/OWenT-s-Utils/tree/master/Bash%26Shell/GCC%20Installer/gcc-4.8)


