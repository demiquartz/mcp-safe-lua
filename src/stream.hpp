// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Takaaki Sato
/**
 * @file  stream.hpp
 * @brief Provides utilities for reading and writing to standard streams.
 */

#pragma once

#include <span>

namespace SafeLua::Stream {

auto Read(std::span<char> buffer) -> std::size_t;
auto Write(std::span<const char> buffer) -> std::size_t;

} // namespace SafeLua::Stream
