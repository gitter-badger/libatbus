﻿//
// Created by 欧文韬 on 2015/6/2.
//

#include <cstring>
#include <memory>
#include <cstdio>
#include "common/file_system.h"
#include "common/compiler_message.h"


#ifdef UTIL_FS_WINDOWS_API
#include <io.h>
#include <direct.h>
#include <Windows.h>

#ifdef _MSC_VER
#include <atlconv.h>
#endif

#ifdef UNICODE
#define VC_TEXT(x) A2W(x)
#else
#define VC_TEXT(x) x
#endif

#define FUNC_ACCESS(x) _access(x, 0)
#define SAFE_STRTOK_S(...) strtok_s(__VA_ARGS__)
#define FUNC_MKDIR(path, mode) _mkdir(path)

#else

#include <dirent.h>
#include <sys/types.h>
#include <errno.h>

#define FUNC_ACCESS(x) access(x, F_OK)
#define SAFE_STRTOK_S(...) strtok_r(__VA_ARGS__)
#define FUNC_MKDIR(path, mode) mkdir(path, mode)

#endif

namespace util {
    bool file_system::get_file_content(std::string& out, const char* file_path, bool is_binary) {
        FILE* f = NULL;
        if(is_binary) {
            UTIL_FS_OPEN(error_code, f, file_path, "rb");
            COMPILER_UNUSED(error_code);
        } else {
            UTIL_FS_OPEN(error_code, f, file_path, "r");
            COMPILER_UNUSED(error_code);
        }

        if (NULL == f) {
            return false;
        }

        fseek(f, 0, SEEK_END);
        long len = ftell(f);
        fseek(f, 0, SEEK_SET);

        out.resize(static_cast<size_t>(len));
        fread(const_cast<char*>(out.data()), sizeof(char), static_cast<size_t>(len), f);

        fclose(f);

        return true;
    }

    bool file_system::split_path(std::vector<std::string>& out, const char* path, bool compact) {
        if (NULL == path) {
            return false;
        }

        char opr_path[MAX_PATH_LEN];

#if defined(UTIL_FS_C11_API)
        strncpy_s(opr_path, sizeof(opr_path), path, strlen(path));
#else
        strncpy(opr_path, path, sizeof(opr_path));
#endif

        char* saveptr = NULL;
        char* token = SAFE_STRTOK_S(opr_path, "\\/", &saveptr);
        while (NULL != token) {
            if (0 != strlen(token)) {

                if (compact) {
                    // 紧缩路径
                    if (0 == strcmp("..", token)) {
                        if (!out.empty() && out.back() != "..") {
                            out.pop_back();
                        } else {
                            out.push_back(token);
                        }
                    } else if (0 != strcmp(".", token)) {
                        out.push_back(token);
                    }
                } else {
                    out.push_back(token);
                }
            }
            token = SAFE_STRTOK_S(NULL, "\\/", &saveptr);
        }

        return !out.empty();
    }

    bool file_system::is_exist(const char* file_path) {
        return 0 == FUNC_ACCESS(file_path);
    }

    bool file_system::mkdir(const char* dir_path, bool recursion, int mode) {
#ifndef UTIL_FS_WINDOWS_API
        if (0 == mode) {
            mode = S_IRWXU | S_IRWXG | S_IRGRP | S_IWGRP | S_IROTH;
        }
#endif
        if (!recursion) {
            return 0 == FUNC_MKDIR(dir_path, mode);
        }

        std::vector<std::string> path_segs;
        split_path(path_segs, dir_path, true);

        if (path_segs.empty()) {
            return false;
        }

        std::string now_path;
        // 留一个\0和一个分隔符位
        now_path.reserve(strlen(dir_path) + 2);

        for(size_t i = 0; i < path_segs.size(); ++ i) {
            now_path += path_segs[i];

            if (false == is_exist(now_path.c_str())) {
                if (false == mkdir(now_path.c_str(), false, mode)) {
                    return false;
                }
            }

            now_path += DIRECTORY_SEPARATOR;
        }

        return true;
    }

    std::string file_system::get_cwd() {
        std::string ret;
#ifdef UTIL_FS_WINDOWS_API
        ret = _getcwd( NULL, 0 );
#else
        ret = getcwd( NULL, 0 );
#endif

        return ret;
    }

    std::string file_system::get_abs_path(const char* dir_path) {
        if (is_abs_path(dir_path)) {
            return dir_path;
        }

        std::string ret;
        ret.reserve(MAX_PATH_LEN);

        std::vector<std::string> out;

        std::string cwd = get_cwd();
        split_path(out, (cwd + DIRECTORY_SEPARATOR + dir_path).c_str(), true);

        if ('\\' == cwd[0] || '/' == cwd[0] ) {
            ret += DIRECTORY_SEPARATOR;
        }

        if (!out.empty()) {
            ret += out[0];
        }

        for (size_t i = 1; i < out.size(); ++ i) {
            ret += DIRECTORY_SEPARATOR;
            ret += out[i];
        }

        return ret;
    }

    bool file_system::rename(const char* from, const char* to) {
        return 0 == ::rename(from, to);
    }

    bool file_system::remove(const char* path) {
        return 0 == ::remove(path);
    }

    FILE* file_system::open_tmp_file() {
#if defined(UTIL_FS_C11_API)
        FILE* ret = NULL;
        if (0 == tmpfile_s(&ret)) {
            return ret;
        }

        return NULL;
#else
        return tmpfile();
#endif
    }

    int file_system::scan_dir(const char* dir_path, std::list<std::string>& out, int options) {
        int ret = 0;
        std::string base_dir = dir_path? dir_path: "";

        // 转为绝对路径
        if ((options & dir_opt_t::EN_DOT_ABSP) && false == is_abs_path(base_dir.c_str())) {
            if (base_dir.empty()) {
                base_dir = get_cwd();
            } else {
                base_dir = get_abs_path(base_dir.c_str());
            }
        }

#ifdef UTIL_FS_WINDOWS_API

        if (!base_dir.empty()) {
            base_dir += DIRECTORY_SEPARATOR;
        }

        _finddata_t child_node;
        intptr_t cache = _findfirst((base_dir + "*").c_str(), &child_node);

        if (-1 == cache) {
            return errno;
        }

        do {

            std::string child_path;
            child_path.reserve(MAX_PATH_LEN);
            child_path = base_dir;
            child_path += child_node.name;

            int accept = 0;
            bool is_link = false;

            // Windows 版本暂不支持软链接
            if (_A_SUBDIR & child_node.attrib) {
                accept = options & dir_opt_t::EN_DOT_TDIR;
            } else if (_A_NORMAL & child_node.attrib) {
                accept = options & dir_opt_t::EN_DOT_TREG;
            } else {
                accept = options & dir_opt_t::EN_DOT_TOTH;
            }

#ifdef _MSC_VER
            USES_CONVERSION;
#endif

            DWORD flag = GetFileAttributes(VC_TEXT(child_path.c_str()));
            if (FILE_ATTRIBUTE_REPARSE_POINT & flag) {
                accept = options & dir_opt_t::EN_DOT_TLNK;
                is_link = true;
            }

            // 类型不符合则跳过
            if (0 == accept) {
                continue;
            }

            // 是否排除 . 和 ..
            if (0 == strcmp(".", child_node.name) || 0 == strcmp("..", child_node.name)) {
                if (!(options & dir_opt_t::EN_DOT_SELF)) {
                    continue;
                }
            } else {

                // 递归扫描（软链接不扫描，防止死循环）
                if (!is_link && (_A_SUBDIR & child_node.attrib) && (options & dir_opt_t::EN_DOT_RECU)) {
                    scan_dir(child_path.c_str(), out, options & (~dir_opt_t::EN_DOT_SELF));
                    continue;
                }

                // 解析软链接
                if ((FILE_ATTRIBUTE_REPARSE_POINT & flag) && (options & dir_opt_t::EN_DOT_RLNK)) {
                    child_path = get_abs_path(child_path.c_str());
                }
            }

            // 普通追加目录
            out.push_back(child_path);
        } while((ret = _findnext( cache, &child_node )) == 0);

        _findclose(cache);

        if (ENOENT == ret) {
            return 0;
        }
#else
        DIR* dir = NULL;
        if (base_dir.empty()) {
            dir = opendir(".");
        } else {
            dir = opendir(base_dir.c_str());
        }
        if (NULL == dir) {
            return errno;
        }

        struct dirent child_node;
        struct dirent* cache = NULL;

        do {
            child_node.d_name[0] = '\0';
            ret = readdir_r(dir, &child_node, &cache);
            if(ret < 0) {
                break;
            }

            // reach the end
            if (NULL == cache) {
                break;
            }

            int accept = 0;
            switch (child_node.d_type) {
                case DT_DIR: {
                    accept = options & dir_opt_t::EN_DOT_TDIR;
                    break;
                }

                case DT_REG: {
                    accept = options & dir_opt_t::EN_DOT_TREG;
                    break;
                }

                case DT_LNK: {
                    accept = options & dir_opt_t::EN_DOT_TLNK;
                    break;
                }

                case DT_SOCK: {
                    accept = options & dir_opt_t::EN_DOT_TSOCK;
                    break;
                }

                default: {
                    accept = options & dir_opt_t::EN_DOT_TOTH;
                    break;
                }
            }

            // 类型不符合则跳过
            if (0 == accept) {
                continue;
            }

            std::string child_path;
            child_path.reserve(MAX_PATH_LEN);
            if (!base_dir.empty()) {
                child_path += base_dir + DIRECTORY_SEPARATOR;
            }
            child_path += child_node.d_name;

            // 是否排除 . 和 ..
            if (0 == strcmp(".", child_node.d_name) || 0 == strcmp("..", child_node.d_name)) {
                if (!(options & dir_opt_t::EN_DOT_SELF)) {
                    continue;
                }
            } else {
                // 递归扫描（软链接不扫描，防止死循环）
                if (DT_DIR == child_node.d_type && (options & dir_opt_t::EN_DOT_RECU)) {
                    scan_dir(child_path.c_str(), out, options & (~dir_opt_t::EN_DOT_SELF));
                    continue;
                }

                // 解析软链接
                if (DT_LNK == child_node.d_type && (options & dir_opt_t::EN_DOT_RLNK)) {
                    child_path = get_abs_path(child_path.c_str());
                }
            }

            // 普通追加目录
            out.push_back(child_path);
        } while(true);

        closedir(dir);

#endif

        return ret;
    }


    bool file_system::is_abs_path(const char* dir_path) {
        if (NULL == dir_path) {
            return false;
        }

        if(dir_path[0] == '/') {
            return true;
        }

#ifdef _WIN32
        if (((dir_path[0] >= 'a' && dir_path[0] <= 'z') || (dir_path[0] >= 'A' && dir_path[0] <= 'Z')) && dir_path[1] == ':' ) {
            return true;
        }
#endif

        return false;
    }
}
