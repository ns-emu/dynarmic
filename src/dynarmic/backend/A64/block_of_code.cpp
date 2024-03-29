/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include "dynarmic/backend/A64/block_of_code.h"

#include <array>
#include <cstring>
#include <limits>

#include <mcl/assert.hpp>

#include "dynarmic/backend/A64/a32_jitstate.h"
#include "dynarmic/backend/A64/abi.h"
#include "dynarmic/backend/A64/perf_map.h"
#include "dynarmic/interface/halt_reason.h"

namespace Dynarmic::BackendA64 {

const Arm64Gen::ARM64Reg BlockOfCode::ABI_RETURN = Arm64Gen::ARM64Reg::X0;
const Arm64Gen::ARM64Reg BlockOfCode::ABI_RETURN2 = Arm64Gen::ARM64Reg::X1;

const Arm64Gen::ARM64Reg BlockOfCode::ABI_PARAM1 = Arm64Gen::ARM64Reg::X0;
const Arm64Gen::ARM64Reg BlockOfCode::ABI_PARAM2 = Arm64Gen::ARM64Reg::X1;
const Arm64Gen::ARM64Reg BlockOfCode::ABI_PARAM3 = Arm64Gen::ARM64Reg::X2;
const Arm64Gen::ARM64Reg BlockOfCode::ABI_PARAM4 = Arm64Gen::ARM64Reg::X3;
const Arm64Gen::ARM64Reg BlockOfCode::ABI_PARAM5 = Arm64Gen::ARM64Reg::X4;
const Arm64Gen::ARM64Reg BlockOfCode::ABI_PARAM6 = Arm64Gen::ARM64Reg::X5;
const Arm64Gen::ARM64Reg BlockOfCode::ABI_PARAM7 = Arm64Gen::ARM64Reg::X6;
const Arm64Gen::ARM64Reg BlockOfCode::ABI_PARAM8 = Arm64Gen::ARM64Reg::X7;

const Arm64Gen::ARM64Reg BlockOfCode::ABI_SCRATCH1 = Arm64Gen::ARM64Reg::X30;

const std::array<Arm64Gen::ARM64Reg, 8> BlockOfCode::ABI_PARAMS = {BlockOfCode::ABI_PARAM1, BlockOfCode::ABI_PARAM2,
                                                                   BlockOfCode::ABI_PARAM3, BlockOfCode::ABI_PARAM4,
                                                                   BlockOfCode::ABI_PARAM5, BlockOfCode::ABI_PARAM6,
                                                                   BlockOfCode::ABI_PARAM7, BlockOfCode::ABI_PARAM8};

namespace {

constexpr size_t TOTAL_CODE_SIZE = 128 * 1024 * 1024;
constexpr size_t FAR_CODE_OFFSET = 100 * 1024 * 1024;

}  // anonymous namespace

BlockOfCode::BlockOfCode(RunCodeCallbacks cb, JitStateInfo jsi)
        : fp_emitter(this)
        , cb(std::move(cb))
        , jsi(jsi)
        , constant_pool(*this) {
    AllocCodeSpace(TOTAL_CODE_SIZE);
    EnableWriting();
    GenRunCode();
}

void BlockOfCode::PreludeComplete() {
    prelude_complete = true;
    near_code_begin = GetCodePtr();
    far_code_begin = GetCodePtr() + FAR_CODE_OFFSET;
    FlushIcache();
    ClearCache();
    DisableWriting();
}

void BlockOfCode::ClearCache() {
    ASSERT(prelude_complete);
    in_far_code = false;
    near_code_ptr = near_code_begin;
    far_code_ptr = far_code_begin;
    SetCodePtr(near_code_begin);
    constant_pool.Clear();
}

size_t BlockOfCode::SpaceRemaining() const {
    ASSERT(prelude_complete);
    // This function provides an underestimate of near-code-size but that's okay.
    // (Why? The maximum size of near code should be measured from near_code_begin, not top_.)
    // These are offsets from Xbyak::CodeArray::top_.
    std::size_t far_code_offset, near_code_offset;
    if (in_far_code) {
        near_code_offset = static_cast<const u8*>(near_code_ptr) - static_cast<const u8*>(region);
        far_code_offset = GetCodePtr() - static_cast<const u8*>(region);
    } else {
        near_code_offset = GetCodePtr() - static_cast<const u8*>(region);
        far_code_offset = static_cast<const u8*>(far_code_ptr) - static_cast<const u8*>(region);
    }
    if (far_code_offset > TOTAL_CODE_SIZE)
        return 0;
    if (near_code_offset > FAR_CODE_OFFSET)
        return 0;
    return std::min(TOTAL_CODE_SIZE - far_code_offset, FAR_CODE_OFFSET - near_code_offset);
}

HaltReason BlockOfCode::RunCode(void* jit_state, CodePtr code_ptr) const {
    return run_code(jit_state, code_ptr);
}

HaltReason BlockOfCode::StepCode(void* jit_state, CodePtr code_ptr) const {
    return step_code(jit_state, code_ptr);
}

void BlockOfCode::ReturnFromRunCode(bool fpscr_already_exited) {
    size_t index = 0;
    if (fpscr_already_exited)
        index |= FPSCR_ALREADY_EXITED;
    B(return_from_run_code[index]);
}

void BlockOfCode::ForceReturnFromRunCode(bool fpscr_already_exited) {
    size_t index = FORCE_RETURN;
    if (fpscr_already_exited)
        index |= FPSCR_ALREADY_EXITED;
    B(return_from_run_code[index]);
}

void BlockOfCode::GenRunCode() {
    const u8 *loop, *enter_fpscr_then_loop;
    std::vector<Arm64Gen::FixupBranch> return_to_caller_fpscr_already_exited;

    AlignCode16();
    run_code = reinterpret_cast<RunCodeFuncType>(GetWritableCodePtr());

    // This serves two purposes:
    // 1. It saves all the registers we as a callee need to save.
    // 2. It aligns the stack so that the code the JIT emits can assume
    //    that the stack is appropriately aligned for CALLs.
    ABI_PushCalleeSaveRegistersAndAdjustStack(*this);

    MOV(Arm64Gen::X28, ABI_PARAM1);
    MOVI2R(Arm64Gen::X27, cb.value_in_X27);
    MOV(Arm64Gen::X25, ABI_PARAM2);  // save temporarily in non-volatile register

    cb.GetTicksRemaining->EmitCall(*this);
    STR(Arm64Gen::INDEX_UNSIGNED, ABI_RETURN, Arm64Gen::X28, jsi.offsetof_cycles_to_run);
    MOV(Arm64Gen::X26, ABI_RETURN);

    LDR(Arm64Gen::INDEX_UNSIGNED, DecodeReg(ABI_SCRATCH1), Arm64Gen::X28, jsi.offsetof_halt_reason);
    return_to_caller_fpscr_already_exited.push_back(CBNZ(ABI_SCRATCH1));

    SwitchFpscrOnEntry();
    BR(Arm64Gen::X25);

    AlignCode16();
    step_code = reinterpret_cast<RunCodeFuncType>(GetWritableCodePtr());
    ABI_PushCalleeSaveRegistersAndAdjustStack(*this);

    MOV(Arm64Gen::X28, ABI_PARAM1);

    MOVI2R(Arm64Gen::X26, 1);
    STR(Arm64Gen::INDEX_UNSIGNED, Arm64Gen::X26, Arm64Gen::X28, jsi.offsetof_cycles_to_run);

    LDR(Arm64Gen::INDEX_UNSIGNED, DecodeReg(ABI_SCRATCH1), Arm64Gen::X28, jsi.offsetof_halt_reason);
    return_to_caller_fpscr_already_exited.push_back(CBNZ(ABI_SCRATCH1));
    ORRI2R(ABI_SCRATCH1, ABI_SCRATCH1, static_cast<u32>(HaltReason::Step));
    STR(Arm64Gen::INDEX_UNSIGNED, DecodeReg(ABI_SCRATCH1), Arm64Gen::X28, jsi.offsetof_halt_reason);

    SwitchFpscrOnEntry();
    BR(ABI_PARAM2);

    enter_fpscr_then_loop = GetCodePtr();
    SwitchFpscrOnEntry();
    loop = GetCodePtr();
    cb.LookupBlock->EmitCall(*this);
    BR(ABI_RETURN);

    // Return from run code variants
    const auto emit_return_from_run_code = [this, &loop, &enter_fpscr_then_loop](bool fpscr_already_exited, bool force_return) {
        Arm64Gen::FixupBranch return_to_caller;
        if (!force_return) {
            LDR(Arm64Gen::INDEX_UNSIGNED, DecodeReg(ABI_SCRATCH1), Arm64Gen::X28, jsi.offsetof_halt_reason);
            return_to_caller = CBNZ(ABI_SCRATCH1);
            CMP(Arm64Gen::X26, Arm64Gen::ZR);
            B(CC_GT, fpscr_already_exited ? enter_fpscr_then_loop : loop);
            SetJumpTarget(return_to_caller);
        }

        if (!fpscr_already_exited) {
            SwitchFpscrOnExit();
        }
        cb.AddTicks->EmitCall(*this, [this](RegList param) {
            LDR(Arm64Gen::INDEX_UNSIGNED, param[0], Arm64Gen::X28, jsi.offsetof_cycles_to_run);
            SUB(param[0], param[0], Arm64Gen::X26);
        });

        LDR(Arm64Gen::INDEX_UNSIGNED, DecodeReg(ABI_RETURN), Arm64Gen::X28, jsi.offsetof_halt_reason);
        // TODO: lock a mutex
        STR(Arm64Gen::INDEX_UNSIGNED, Arm64Gen::WZR, Arm64Gen::X28, jsi.offsetof_halt_reason);

        ABI_PopCalleeSaveRegistersAndAdjustStack(*this);
        RET();
    };

    return_from_run_code[0] = AlignCode16();
    emit_return_from_run_code(false, false);

    return_from_run_code[FPSCR_ALREADY_EXITED] = AlignCode16();
    emit_return_from_run_code(true, false);

    return_from_run_code[FORCE_RETURN] = AlignCode16();
    emit_return_from_run_code(false, true);

    return_from_run_code[FPSCR_ALREADY_EXITED | FORCE_RETURN] = AlignCode16();
    for (const auto& jump_target : return_to_caller_fpscr_already_exited)
        SetJumpTarget(jump_target);
    emit_return_from_run_code(true, true);

    PerfMapRegister(run_code, GetCodePtr(), "dynarmic_dispatcher");
}

void BlockOfCode::SwitchFpscrOnEntry() {
    MRS(ABI_SCRATCH1, Arm64Gen::FIELD_FPCR);
    STR(Arm64Gen::INDEX_UNSIGNED, ABI_SCRATCH1, Arm64Gen::X28, jsi.offsetof_save_host_FPCR);

    LDR(Arm64Gen::INDEX_UNSIGNED, ABI_SCRATCH1, Arm64Gen::X28, jsi.offsetof_guest_fpcr);
    _MSR(Arm64Gen::FIELD_FPCR, ABI_SCRATCH1);
    LDR(Arm64Gen::INDEX_UNSIGNED, ABI_SCRATCH1, Arm64Gen::X28, jsi.offsetof_guest_fpsr);
    _MSR(Arm64Gen::FIELD_FPSR, ABI_SCRATCH1);
}

void BlockOfCode::SwitchFpscrOnExit() {
    MRS(ABI_SCRATCH1, Arm64Gen::FIELD_FPCR);
    STR(Arm64Gen::INDEX_UNSIGNED, ABI_SCRATCH1, Arm64Gen::X28, jsi.offsetof_guest_fpcr);
    MRS(ABI_SCRATCH1, Arm64Gen::FIELD_FPSR);
    STR(Arm64Gen::INDEX_UNSIGNED, ABI_SCRATCH1, Arm64Gen::X28, jsi.offsetof_guest_fpsr);

    LDR(Arm64Gen::INDEX_UNSIGNED, ABI_SCRATCH1, Arm64Gen::X28, jsi.offsetof_save_host_FPCR);
    _MSR(Arm64Gen::FIELD_FPCR, ABI_SCRATCH1);
}

void BlockOfCode::UpdateTicks() {
    cb.AddTicks->EmitCall(*this, [this](RegList param) {
        LDR(Arm64Gen::INDEX_UNSIGNED, param[0], Arm64Gen::X28, jsi.offsetof_cycles_to_run);
        SUB(param[0], param[0], Arm64Gen::X26);
    });

    cb.GetTicksRemaining->EmitCall(*this);
    STR(Arm64Gen::INDEX_UNSIGNED, ABI_RETURN, Arm64Gen::X28, jsi.offsetof_cycles_to_run);
    MOV(Arm64Gen::X26, ABI_RETURN);
}

void BlockOfCode::LookupBlock() {
    cb.LookupBlock->EmitCall(*this);
}

void BlockOfCode::EmitPatchLDR(Arm64Gen::ARM64Reg Rt, u64 lower, u64 upper) {
    ASSERT_MSG(!in_far_code, "Can't patch when in far code, yet!");
    constant_pool.EmitPatchLDR(Rt, lower, upper);
}

void BlockOfCode::PatchConstPool() {
    constant_pool.PatchPool();
}

void BlockOfCode::SwitchToFarCode() {
    ASSERT(prelude_complete);
    ASSERT(!in_far_code);
    in_far_code = true;
    near_code_ptr = GetCodePtr();
    SetCodePtr(far_code_ptr);

    ASSERT_MSG(near_code_ptr < far_code_begin, "Near code has overwritten far code!");
}

void BlockOfCode::SwitchToNearCode() {
    ASSERT(prelude_complete);
    ASSERT(in_far_code);
    in_far_code = false;
    far_code_ptr = GetCodePtr();
    SetCodePtr(near_code_ptr);
}

CodePtr BlockOfCode::GetCodeBegin() const {
    return near_code_begin;
}

u8* BlockOfCode::GetRegion() const {
    return region;
}

std::size_t BlockOfCode::GetRegionSize() const {
    return total_region_size;
}

void* BlockOfCode::AllocateFromCodeSpace(size_t alloc_size) {
    ASSERT_MSG(GetSpaceLeft() >= alloc_size, "ERR_CODE_IS_TOO_BIG");

    void* ret = GetWritableCodePtr();
    region_size += alloc_size;
    SetCodePtr(GetCodePtr() + alloc_size);
    memset(ret, 0, alloc_size);
    return ret;
}

void BlockOfCode::SetCodePtr(CodePtr code_ptr) {
    u8* ptr = const_cast<u8*>(reinterpret_cast<const u8*>(code_ptr));
    ARM64XEmitter::SetCodePtr(ptr);
}

void BlockOfCode::EnsurePatchLocationSize(CodePtr begin, size_t size) {
    size_t current_size = GetCodePtr() - reinterpret_cast<const u8*>(begin);
    ASSERT(current_size <= size);
    for (u32 i = 0; i < (size - current_size) / 4; i++) {
        HINT(Arm64Gen::HINT_NOP);
    }
}

//bool BlockOfCode::DoesCpuSupport(Xbyak::util::Cpu::Type type) const {
//#ifdef DYNARMIC_ENABLE_CPU_FEATURE_DETECTION
//    return cpu_info.has(type);
//#else
//    (void)type;
//    return false;
//#endif
//}

}  // namespace Dynarmic::BackendA64
