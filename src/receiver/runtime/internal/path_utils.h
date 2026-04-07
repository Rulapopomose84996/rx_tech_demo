#pragma once

#include <cerrno>
#include <stdexcept>
#include <string>

#include <sys/stat.h>
#include <sys/types.h>

namespace rxtech
{
    namespace path_utils
    {

        inline void create_directory_if_needed(const std::string &path)
        {
            if (path.empty())
            {
                return;
            }
            if (::mkdir(path.c_str(), 0755) != 0 && errno != EEXIST)
            {
                throw std::runtime_error("创建目录失败: " + path);
            }
        }

        /**
         * 确保文件路径的父目录存在，如果不存在则递归创建
         *
         * 该函数会解析给定文件路径的父目录部分，并逐级检查每一层目录是否存在。
         * 对于不存在的目录，会自动创建。这样可以确保在创建文件之前，其所在的
         * 目录结构已经完整建立。
         *
         * @param file_path 文件的完整路径，函数将确保该路径的父目录存在
         *                  - 如果路径中不包含 '/' 分隔符，或 '/' 位于起始位置，则直接返回
         *                  - 支持绝对路径和相对路径
         *                  - 支持多层嵌套的目录结构
         *
         * @return 无返回值
         *
         * @throws std::runtime_error 当创建目录失败且错误原因不是"目录已存在"时抛出异常
         *
         * @note 该函数使用正斜杠 '/' 作为路径分隔符
         * @note 对于根目录 '/' 的情况会特殊处理
         * @note 连续的多个 '/' 会被视为单个分隔符处理
         */
        inline void ensure_parent_directory(const std::string &file_path)
        {
            // 查找最后一个路径分隔符的位置
            const std::size_t sep = file_path.find_last_of('/');
            if (sep == std::string::npos || sep == 0U)
            {
                return;
            }

            // 提取父目录路径
            const std::string parent = file_path.substr(0U, sep);
            std::size_t start = parent[0] == '/' ? 1U : 0U;
            std::string current = parent.substr(0U, start);

            // 逐级遍历父目录的每一层，确保每层目录都存在
            while (start < parent.size())
            {
                // 跳过连续的路径分隔符
                while (start < parent.size() && parent[start] == '/')
                {
                    if (current.empty())
                    {
                        current.push_back('/');
                    }
                    ++start;
                }

                // 提取当前层级的目录名
                const std::size_t next = parent.find('/', start);
                const std::string part =
                    parent.substr(start, next == std::string::npos ? std::string::npos : next - start);
                if (!part.empty())
                {
                    // 构建当前完整路径并创建目录
                    if (!current.empty() && current.back() != '/')
                    {
                        current.push_back('/');
                    }
                    current += part;
                    create_directory_if_needed(current);
                }

                // 如果已到达路径末尾，退出循环
                if (next == std::string::npos)
                {
                    break;
                }
                start = next + 1U;
            }
        }

    } // namespace path_utils
} // namespace rxtech
