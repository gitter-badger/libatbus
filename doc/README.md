# 使用文档

## 编译构建
### 准备工作
+ 支持或部分支持C++11的编译器，至少要支持thread、atomic、智能指针、函数绑定、static_assert (如：GCC 4.4以上、VC 10以上、clang 3.0 以上等)
+ cmake: 2.8.9 以上,如果系统自带cmake版本过低请手动编译安装[cmake](http://cmake.org/)，
+ protocol文件夹内的协议文件是事先生成好的，当然也可以重新生成一份放里面

### 提示
+ GCC 4.4 以上会自动使用-std=gnu++0x, GCC 4.7 以上C++采用 -std=gnu++11， c采用 -std=gnu11。但不会开启C++1y/C++14标准
+ Clang会自动开启到-std=c++11

### 编译选项
除了cmake标准编译选项外，libatbus还提供一些额外选项

+ ATBUS_MACRO_BUSID_TYPE: busid的类型(默认: uint64_t)，建议不要设置成大于64位，否则需要修改protocol目录内的busid类型，并且重新生成协议文件
+ GTEST_ROOT: 使用GTest单元测试框架
+ BOOST_ROOT: 设置Boost库根目录
+ PROJECT_TEST_ENABLE_BOOST_UNIT_TEST: 使用Boost.Test单元测试框架(如果GTEST_ROOT和此项都不设置，则使用内置单元测试框架)


## 开发文档
### 目录结构说明

+ 3rd_party: 外部组件（不一定是依赖项）
+ doc: 文档目录
+ include: 导出lib的包含文件（注意不导出的内部接口后文件会直接在src目录里）
+ project: 工程工具和配置文件集
+ protocol: 协议描述文件目录
+ sample: 使用示例目录，每一个cpp文件都是一个完全独立的例子
+ src: 源文件和内部接口申明目录
+ test: 测试框架及测试用例目录

### 关于 #pragma once
由于目标平台和环境的编译器均已支持 #pragma once 功能，故而所有源代码直接使用这个关键字，以提升编译速度。

详见:[pragma once](http://zh.wikipedia.org/wiki/Pragma_once) 

