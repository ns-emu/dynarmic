/* This file is part of the dynarmic project.
 * Copyright (c) 2022 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#include "dynarmic/backend/A64/abi.h"
#include "dynarmic/backend/A64/emitter/a64_emitter.h"
#include "dynarmic/backend/A64/hostloc.h"
#include "dynarmic/common/spin_lock.h"

namespace Dynarmic {

using BackendA64::Arm64Gen::ARM64CodeBlock;
using BackendA64::Arm64Gen::ARM64Reg;

void EmitSpinLockLock(ARM64CodeBlock& code, ARM64Reg ptr, ARM64Reg tmp1, ARM64Reg tmp2) {
    BackendA64::Arm64Gen::FixupBranch start;
    const u8* loop;

    ARM64Reg tmp1_32 = BackendA64::Arm64Gen::EncodeRegTo32(tmp1);
    ARM64Reg tmp2_32 = BackendA64::Arm64Gen::EncodeRegTo32(tmp2);

    // Adapted from arm reference manual impl
    code.MOVI2R(tmp1_32, 1);
    code.HINT(BackendA64::Arm64Gen::HINT_SEVL);
    start = code.B();
    loop = code.GetCodePtr();
    code.HINT(BackendA64::Arm64Gen::HINT_WFE);
    code.SetJumpTarget(start);
    code.LDAXR(tmp2_32, ptr);
    code.CBNZ(tmp2_32, loop);
    code.STXR(tmp2_32, tmp1_32, ptr);
    code.CBNZ(tmp2_32, loop);
}

void EmitSpinLockUnlock(ARM64CodeBlock& code, ARM64Reg ptr) {
    code.STLR(ARM64Reg::WZR, ptr);
}

namespace {

struct SpinLockImpl {
    SpinLockImpl();

    ARM64CodeBlock code;
    void (*lock)(volatile int*);
    void (*unlock)(volatile int*);
};

SpinLockImpl impl;

SpinLockImpl::SpinLockImpl() {
    code.AllocCodeSpace(64);
    code.EnableWriting();
    const ARM64Reg ABI_PARAM1 = BackendA64::HostLocToReg64(BackendA64::ABI_PARAM1);
    code.AlignCode16();
    lock = reinterpret_cast<void (*)(volatile int*)>(code.GetWritableCodePtr());
    EmitSpinLockLock(code, ABI_PARAM1, ARM64Reg::W8, ARM64Reg::W9);
    code.RET();

    code.AlignCode16();
    unlock = reinterpret_cast<void (*)(volatile int*)>(code.GetWritableCodePtr());
    EmitSpinLockUnlock(code, ABI_PARAM1);
    code.RET();
    code.DisableWriting();
    code.FlushIcache();
}

}  // namespace

void SpinLock::Lock() {
    impl.lock(&storage);
}

void SpinLock::Unlock() {
    impl.unlock(&storage);
}

}  // namespace Dynarmic
