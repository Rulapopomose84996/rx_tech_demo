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
                throw std::runtime_error("failed to create directory: " + path);
            }
        }

        inline void ensure_parent_directory(const std::string &file_path)
        {
            const std::size_t sep = file_path.find_last_of('/');
            if (sep == std::string::npos || sep == 0U)
            {
                return;
            }
            const std::string parent = file_path.substr(0U, sep);
            std::size_t start = parent[0] == '/' ? 1U : 0U;
            std::string current = parent.substr(0U, start);

            while (start < parent.size())
            {
                while (start < parent.size() && parent[start] == '/')
                {
                    if (current.empty())
                    {
                        current.push_back('/');
                    }
                    ++start;
                }
                const std::size_t next = parent.find('/', start);
                const std::string part =
                    parent.substr(start, next == std::string::npos ? std::string::npos : next - start);
                if (!part.empty())
                {
                    if (!current.empty() && current.back() != '/')
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
