#pragma once

#include <cstddef>
#include <cerrno>
#include <stdexcept>
#include <string>

#include <sys/stat.h>
#include <sys/types.h>

namespace rxtech
{
    namespace path_utils
    {

        inline bool is_path_separator(char ch)
        {
            return ch == '/' || ch == '\\';
        }

        inline std::string trim_trailing_separators(std::string path)
        {
            while (!path.empty() && is_path_separator(path.back()))
            {
                path.pop_back();
            }
            return path;
        }

        inline std::string path_filename(const std::string &path)
        {
            const std::string normalized = trim_trailing_separators(path);
            const std::size_t pos = normalized.find_last_of("/\\");
            return pos == std::string::npos ? normalized : normalized.substr(pos + 1U);
        }

        inline std::string path_parent(const std::string &path)
        {
            const std::string normalized = trim_trailing_separators(path);
            const std::size_t pos = normalized.find_last_of("/\\");
            if (pos == std::string::npos)
            {
                return std::string{};
            }
            if (pos == 0U && is_path_separator(normalized.front()))
            {
                return normalized.substr(0U, 1U);
            }
            return normalized.substr(0U, pos);
        }

        inline std::string join_path(const std::string &base, const std::string &name)
        {
            if (base.empty())
            {
                return name;
            }
            if (is_path_separator(base.back()))
            {
                return base + name;
            }
            return base + "/" + name;
        }

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
            const std::string parent = path_parent(file_path);
            if (parent.empty() || parent == "/")
            {
                return;
            }

            std::size_t start = 0U;
            std::string current;
            if (is_path_separator(parent[0]))
            {
                current = "/";
                start = 1U;
            }
            else if (parent.size() > 1U && parent[1] == ':')
            {
                current = parent.substr(0U, 2U);
                start = 2U;
            }

            while (start < parent.size())
            {
                while (start < parent.size() && is_path_separator(parent[start]))
                {
                    if (current.empty())
                    {
                        current.push_back('/');
                    }
                    ++start;
                }

                const std::size_t next = parent.find_first_of("/\\", start);
                const std::string part =
                    parent.substr(start, next == std::string::npos ? std::string::npos : next - start);
                if (!part.empty())
                {
                    if (!current.empty() && !is_path_separator(current.back()))
                    {
                        current.push_back('/');
                    }
                    current += part;
                    create_directory_if_needed(current);
                }

                if (next == std::string::npos)
                {
                    break;
                }
                start = next + 1U;
            }
        }

    } // namespace path_utils
} // namespace rxtech
