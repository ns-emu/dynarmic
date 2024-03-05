/* This file is part of the dynarmic project.
 * Copyright (c) 2022 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#pragma once

namespace Dynarmic {

void EmitSpinLockLock(BackendA64::Arm64Gen::ARM64CodeBlock& code, BackendA64::Arm64Gen::ARM64Reg ptr, BackendA64::Arm64Gen::ARM64Reg tmp1, BackendA64::Arm64Gen::ARM64Reg tmp2);
void EmitSpinLockUnlock(BackendA64::Arm64Gen::ARM64CodeBlock& code, BackendA64::Arm64Gen::ARM64Reg ptr, BackendA64::Arm64Gen::ARM64Reg tmp);

}  // namespace Dynarmic
