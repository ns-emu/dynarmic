/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#pragma once

#include <cstddef>
#include <string>

#include <mcl/bit_cast.hpp>

namespace Dynarmic::BackendA64 {

namespace detail {
void PerfMapRegister(const void* start, const void* end, const std::string& friendly_name);
} // namespace detail

template<typename T>
void PerfMapRegister(T start, const void* end, const std::string& friendly_name) {
    detail::PerfMapRegister(mcl::bit_cast<const void*>(start), end, friendly_name);
}

void PerfMapClear();

} // namespace Dynarmic::BackendX64
