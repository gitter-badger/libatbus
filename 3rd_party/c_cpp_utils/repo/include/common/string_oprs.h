/**
 * @file string_oprs.h
 * @brief 字符串相关操作
 * Licensed under the MIT licenses.
 *
 * @version 1.0
 * @author owent
 * @date 2015.11.24
 *
 * @history
 *
 *
 */

#ifndef _UTIL_COMMON_STRING_OPRS_H_
#define _UTIL_COMMON_STRING_OPRS_H_

// 目测主流编译器都支持且有优化， gcc 3.4 and upper, vc, clang, c++ builder xe3, intel c++ and etc.
//#if defined(_MSC_VER) && (_MSC_VER >= 1020)
# pragma once
//#endif

#include <cstdlib>
#include <cstring>
#include <algorithm>

#ifdef _MSC_VER
#define ATBUS_FUNC_STRCASE_CMP(l, r) _stricmp(l, r)
#define ATBUS_FUNC_STRNCASE_CMP(l, r, s) _strnicmp(l, r, s)
#define ATBUS_FUNC_SSCANF(...) sscanf_s(__VA_ARGS__)
#define ATBUS_FUNC_SNPRINTF(...) sprintf_s(__VA_ARGS__)
#else
#define ATBUS_FUNC_STRCASE_CMP(l, r) strcasecmp(l, r)
#define ATBUS_FUNC_STRNCASE_CMP(l, r, s) strncasecmp(l, r, s)
#define ATBUS_FUNC_SSCANF(...) sscanf(__VA_ARGS__)
#define ATBUS_FUNC_SNPRINTF(...) snprintf(__VA_ARGS__)
#endif

namespace util {
    namespace string {
        /**
        * @brief 字符串转整数
        * @param out 输出的整数
        * @param str 被转换的字符串
        * @note 性能肯定比sscanf系，和iostream系高。strtol系就不知道了
        */
        template<typename T>
        void str2int(T& out, const char* str) {
            out = static_cast<T>(0);
            if (NULL == str || !(*str)) {
                return;
            }

            if ('0' == str[0] && 'x' == str[1]) { // hex
                for (size_t i = 2; str[i]; ++i) {
                    char c = static_cast<char>(::tolower(str[i]));
                    if (str[i] >= '0' && str[i] <= '9') {
                        out <<= 4;
                        out += str[i] - '0';
                    } else if (str[i] >= 'a' && str[i] <= 'f') {
                        out <<= 4;
                        out += str[i] - 'a' + 10;
                    } else {
                        break;
                    }
                }
            } else if ('\\' == str[0]) { // oct
                for (size_t i = 0; str[i] >= '0' && str[i] < '8'; ++i) {
                    out <<= 3;
                    out += str[i] - '0';
                }
            } else { // dec
                for (size_t i = 0; str[i] >= '0' && str[i] <= '9'; ++i) {
                    out *= 10;
                    out += str[i] - '0';
                }
            }
        }
    }
}

#endif /* _UTIL_COMMON_COMPILER_MESSAGE_H_ */
