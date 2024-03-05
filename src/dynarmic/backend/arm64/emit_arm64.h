/* This file is part of the dynarmic project.
 * Copyright (c) 2022 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#pragma once

#include <array>
#include <cstddef>
#include <memory>
#include <vector>

#include <mcl/stdint.hpp>
#include <tsl/robin_map.h>

#include "dynarmic/interface/A32/coprocessor.h"
#include "dynarmic/interface/optimization_flags.h"
#include "dynarmic/ir/location_descriptor.h"

namespace oaknut {
struct PointerCodeGeneratorPolicy;
template<typename>
class BasicCodeGenerator;
using CodeGenerator = BasicCodeGenerator<PointerCodeGeneratorPolicy>;
struct Label;
}  // namespace oaknut

namespace Dynarmic::FP {
class FPCR;
}  // namespace Dynarmic::FP

namespace Dynarmic::IR {
class Block;
class Inst;
enum class Cond;
enum class Opcode;
}  // namespace Dynarmic::IR

namespace Dynarmic::Backend::Arm64 {

using CodePtr = std::byte*;

enum class LinkTarget {
    ReturnToDispatcher,
    ReturnFromRunCode,
    ReadMemory8,
    ReadMemory16,
    ReadMemory32,
    ReadMemory64,
    ExclusiveReadMemory8,
    ExclusiveReadMemory16,
    ExclusiveReadMemory32,
    ExclusiveReadMemory64,
    WriteMemory8,
    WriteMemory16,
    WriteMemory32,
    WriteMemory64,
    ExclusiveWriteMemory8,
    ExclusiveWriteMemory16,
    ExclusiveWriteMemory32,
    ExclusiveWriteMemory64,
    CallSVC,
    ExceptionRaised,
    InstructionSynchronizationBarrierRaised,
    AddTicks,
    GetTicksRemaining,
};

struct Relocation {
    std::ptrdiff_t code_offset;
    LinkTarget target;
};

struct BlockRelocation {
    std::ptrdiff_t code_offset;
};

struct EmittedBlockInfo {
    CodePtr entry_point;
    size_t size;
    std::vector<Relocation> relocations;
    tsl::robin_map<IR::LocationDescriptor, std::vector<BlockRelocation>> block_relocations;
};

struct EmitConfig {
    bool hook_isb;
    bool enable_cycle_counting;
    bool always_little_endian;

    FP::FPCR (*descriptor_to_fpcr)(const IR::LocationDescriptor& descriptor);

    size_t state_nzcv_offset;
    size_t state_fpsr_offset;

    std::array<std::shared_ptr<A32::Coprocessor>, 16> coprocessors{};

    OptimizationFlag optimizations;

    bool HasOptimization(OptimizationFlag f) const { return (f & optimizations) != no_optimizations; }
};

struct EmitContext;

EmittedBlockInfo EmitArm64(oaknut::CodeGenerator& code, IR::Block block, const EmitConfig& emit_conf);

template<IR::Opcode op>
void EmitIR(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst);
void EmitRelocation(oaknut::CodeGenerator& code, EmitContext& ctx, LinkTarget link_target);
void EmitBlockLinkRelocation(oaknut::CodeGenerator& code, EmitContext& ctx, const IR::LocationDescriptor& descriptor);
oaknut::Label EmitA32Cond(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Cond cond);
void EmitA32Terminal(oaknut::CodeGenerator& code, EmitContext& ctx);
void EmitA32ConditionFailedTerminal(oaknut::CodeGenerator& code, EmitContext& ctx);

}  // namespace Dynarmic::Backend::Arm64
