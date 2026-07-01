// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Takaaki Sato
/**
 * @file  stream.cpp
 * @brief Provides utilities for reading and writing to standard streams.
 */

#include <system_error>

#ifdef _WIN32
#include <io.h>
#define read _read
#define write _write
#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#else
#include <unistd.h>
#endif

#include "stream.hpp"

namespace SafeLua::Stream {

auto Read(std::span<char> buffer) -> std::size_t
{
    while (true) {
        if (auto count = read(STDIN_FILENO, buffer.data(), buffer.size()); count >= 0) {
            return count;
        }
        if (errno != EINTR) {
            throw std::system_error(errno, std::generic_category());
        }
    }
}

auto Write(std::span<const char> buffer) -> std::size_t
{
    while (true) {
        if (auto count = write(STDOUT_FILENO, buffer.data(), buffer.size()); count >= 0) {
            return count;
        }
        if (errno != EINTR) {
            throw std::system_error(errno, std::generic_category());
        }
    }
}

} // namespace SafeLua::Stream
