#pragma once

#ifndef LIBATBUS_DETAIL_LIBATBUS_CONFIG_H_
#define LIBATBUS_DETAIL_LIBATBUS_CONFIG_H_

#ifndef ATBUS_MACRO_MSG_LIMIT
#define ATBUS_MACRO_MSG_LIMIT 65536
#endif

#ifndef ATBUS_MACRO_CONNECTION_CONFIRM_TIMEOUT
#define ATBUS_MACRO_CONNECTION_CONFIRM_TIMEOUT 30000
#endif

#ifndef ATBUS_MACRO_CONNECTION_BACKLOG
#define ATBUS_MACRO_CONNECTION_BACKLOG 128
#endif


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

#if (defined(__cplusplus) && __cplusplus >= 201103L) || (defined(_MSC_VER) && _MSC_VER >= 1600)
#include <unordered_map>
#define ATBUS_ADVANCE_TYPE_MAP(...) std::unordered_map< __VA_ARGS__ >

#else
#include <map>
#define ATBUS_ADVANCE_TYPE_MAP(...) std::map< __VA_ARGS__ >
#endif

#if defined(__cplusplus) && __cplusplus >= 201103L
#define ATBUS_MACRO_ENABLE_STATIC_ASSERT 1
#elif defined(_MSC_VER) && _MSC_VER >= 1600
#define ATBUS_MACRO_ENABLE_STATIC_ASSERT 1
#endif

#endif