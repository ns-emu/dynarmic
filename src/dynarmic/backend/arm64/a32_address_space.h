/* This file is part of the dynarmic project.
 * Copyright (c) 2022 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#pragma once

#include <mcl/stdint.hpp>
#include <oaknut/code_block.hpp>
#include <oaknut/oaknut.hpp>
#include <tsl/robin_map.h>
#include <tsl/robin_set.h>

#include "dynarmic/backend/arm64/emit_arm64.h"
#include "dynarmic/interface/A32/config.h"
#include "dynarmic/interface/halt_reason.h"
#include "dynarmic/ir/basic_block.h"
#include "dynarmic/ir/location_descriptor.h"

namespace Dynarmic::Backend::Arm64 {

struct A32JitState;

class A32AddressSpace final {
public:
    explicit A32AddressSpace(const A32::UserConfig& conf);

    IR::Block GenerateIR(IR::LocationDescriptor) const;

    CodePtr Get(IR::LocationDescriptor descriptor);

    CodePtr GetOrEmit(IR::LocationDescriptor descriptor);

    void ClearCache();

private:
    friend class A32Core;

    void EmitPrelude();

    size_t GetRemainingSize();
    EmittedBlockInfo Emit(IR::Block ir_block);
    void Link(IR::LocationDescriptor block_descriptor, EmittedBlockInfo& block);
    void RelinkForDescriptor(IR::LocationDescriptor target_descriptor);

    const A32::UserConfig conf;

    oaknut::CodeBlock mem;
    oaknut::CodeGenerator code;

    tsl::robin_map<u64, CodePtr> block_entries;
    tsl::robin_map<u64, EmittedBlockInfo> block_infos;
    tsl::robin_map<u64, tsl::robin_set<u64>> block_references;

    struct PreludeInfo {
        u32* end_of_prelude;

        using RunCodeFuncType = HaltReason (*)(CodePtr entry_point, A32JitState* context, volatile u32* halt_reason);
        RunCodeFuncType run_code;
        RunCodeFuncType step_code;
        void* return_to_dispatcher;
        void* return_from_run_code;

        void* read_memory_8;
        void* read_memory_16;
        void* read_memory_32;
        void* read_memory_64;
        void* exclusive_read_memory_8;
        void* exclusive_read_memory_16;
        void* exclusive_read_memory_32;
        void* exclusive_read_memory_64;
        void* write_memory_8;
        void* write_memory_16;
        void* write_memory_32;
        void* write_memory_64;
        void* exclusive_write_memory_8;
        void* exclusive_write_memory_16;
        void* exclusive_write_memory_32;
        void* exclusive_write_memory_64;
        void* call_svc;
        void* exception_raised;
        void* isb_raised;
        void* add_ticks;
        void* get_ticks_remaining;
    } prelude_info;
};

}  // namespace Dynarmic::Backend::Arm64
