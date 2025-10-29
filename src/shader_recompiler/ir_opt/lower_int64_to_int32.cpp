// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <utility>

#include "shader_recompiler/exception.h"
#include "shader_recompiler/frontend/ir/basic_block.h"
#include "shader_recompiler/frontend/ir/ir_emitter.h"
#include "shader_recompiler/frontend/ir/program.h"
#include "shader_recompiler/frontend/ir/value.h"
#include "shader_recompiler/ir_opt/passes.h"

namespace Shader::Optimization {
namespace {
std::pair<IR::U32, IR::U32> Unpack(IR::IREmitter& ir, const IR::Value& packed) {
    if (packed.IsImmediate()) {
        const u64 value{packed.U64()};
        return {
            ir.Imm32(static_cast<u32>(value)),
            ir.Imm32(static_cast<u32>(value >> 32)),
        };
    } else {
        return std::pair<IR::U32, IR::U32>{
            ir.CompositeExtract(packed, 0u),
            ir.CompositeExtract(packed, 1u),
        };
    }
}

void IAdd64To32(IR::Block& block, IR::Inst& inst) {
    if (inst.HasAssociatedPseudoOperation()) {
        throw NotImplementedException("IAdd64 emulation with pseudo instructions");
    }
    IR::IREmitter ir(block, IR::Block::InstructionList::s_iterator_to(inst));
    const auto [a_lo, a_hi]{Unpack(ir, inst.Arg(0))};
    const auto [b_lo, b_hi]{Unpack(ir, inst.Arg(1))};

    const IR::U32 ret_lo{ir.IAdd(a_lo, b_lo)};
    const IR::U32 carry{ir.Select(ir.GetCarryFromOp(ret_lo), ir.Imm32(1u), ir.Imm32(0u))};

    const IR::U32 ret_hi{ir.IAdd(ir.IAdd(a_hi, b_hi), carry)};
    inst.ReplaceUsesWith(ir.CompositeConstruct(ret_lo, ret_hi));
}

void ISub64To32(IR::Block& block, IR::Inst& inst) {
    if (inst.HasAssociatedPseudoOperation()) {
        throw NotImplementedException("ISub64 emulation with pseudo instructions");
    }
    IR::IREmitter ir(block, IR::Block::InstructionList::s_iterator_to(inst));
    const auto [a_lo, a_hi]{Unpack(ir, inst.Arg(0))};
    const auto [b_lo, b_hi]{Unpack(ir, inst.Arg(1))};

    const IR::U32 ret_lo{ir.ISub(a_lo, b_lo)};
    const IR::U1 underflow{ir.IGreaterThan(ret_lo, a_lo, false)};
    const IR::U32 underflow_bit{ir.Select(underflow, ir.Imm32(1u), ir.Imm32(0u))};

    const IR::U32 ret_hi{ir.ISub(ir.ISub(a_hi, b_hi), underflow_bit)};
    inst.ReplaceUsesWith(ir.CompositeConstruct(ret_lo, ret_hi));
}

void INeg64To32(IR::Block& block, IR::Inst& inst) {
    if (inst.HasAssociatedPseudoOperation()) {
        throw NotImplementedException("INeg64 emulation with pseudo instructions");
    }
    IR::IREmitter ir(block, IR::Block::InstructionList::s_iterator_to(inst));
    auto [lo, hi]{Unpack(ir, inst.Arg(0))};
    lo = ir.BitwiseNot(lo);
    hi = ir.BitwiseNot(hi);

    lo = ir.IAdd(lo, ir.Imm32(1));

    const IR::U32 carry{ir.Select(ir.GetCarryFromOp(lo), ir.Imm32(1u), ir.Imm32(0u))};
    hi = ir.IAdd(hi, carry);

    inst.ReplaceUsesWith(ir.CompositeConstruct(lo, hi));
}

void ShiftLeftLogical64To32(IR::Block& block, IR::Inst& inst) {
    if (inst.HasAssociatedPseudoOperation()) {
        throw NotImplementedException("ShiftLeftLogical64 emulation with pseudo instructions");
    }
    IR::IREmitter ir(block, IR::Block::InstructionList::s_iterator_to(inst));
    const auto [lo, hi]{Unpack(ir, inst.Arg(0))};
    const IR::U32 shift{inst.Arg(1)};

    const IR::U32 shifted_lo{ir.ShiftLeftLogical(lo, shift)};
    const IR::U32 shifted_hi{ir.ShiftLeftLogical(hi, shift)};

    const IR::U32 inv_shift{ir.ISub(shift, ir.Imm32(32))};
    const IR::U1 is_long{ir.IGreaterThanEqual(inv_shift, ir.Imm32(0), true)};
    const IR::U1 is_zero{ir.IEqual(shift, ir.Imm32(0))};

    const IR::U32 long_ret_lo{ir.Imm32(0)};
    const IR::U32 long_ret_hi{ir.ShiftLeftLogical(lo, inv_shift)};

    const IR::U32 shift_complement{ir.ISub(ir.Imm32(32), shift)};
    const IR::U32 lo_extract{ir.BitFieldExtract(lo, shift_complement, shift, false)};
    const IR::U32 short_ret_lo{shifted_lo};
    const IR::U32 short_ret_hi{ir.BitwiseOr(shifted_hi, lo_extract)};

    const IR::U32 zero_ret_lo{lo};
    const IR::U32 zero_ret_hi{hi};

    const IR::U32 non_zero_lo{ir.Select(is_long, long_ret_lo, short_ret_lo)};
    const IR::U32 non_zero_hi{ir.Select(is_long, long_ret_hi, short_ret_hi)};

    const IR::U32 ret_lo{ir.Select(is_zero, zero_ret_lo, non_zero_lo)};
    const IR::U32 ret_hi{ir.Select(is_zero, zero_ret_hi, non_zero_hi)};
    inst.ReplaceUsesWith(ir.CompositeConstruct(ret_lo, ret_hi));
}

void ShiftRightLogical64To32(IR::Block& block, IR::Inst& inst) {
    if (inst.HasAssociatedPseudoOperation()) {
        throw NotImplementedException("ShiftRightLogical64 emulation with pseudo instructions");
    }
    IR::IREmitter ir(block, IR::Block::InstructionList::s_iterator_to(inst));
    const auto [lo, hi]{Unpack(ir, inst.Arg(0))};
    const IR::U32 shift{inst.Arg(1)};

    const IR::U32 shifted_lo{ir.ShiftRightLogical(lo, shift)};
    const IR::U32 shifted_hi{ir.ShiftRightLogical(hi, shift)};

    const IR::U32 inv_shift{ir.ISub(shift, ir.Imm32(32))};
    const IR::U1 is_long{ir.IGreaterThanEqual(inv_shift, ir.Imm32(0), true)};
    const IR::U1 is_zero{ir.IEqual(shift, ir.Imm32(0))};

    const IR::U32 long_ret_hi{ir.Imm32(0)};
    const IR::U32 long_ret_lo{ir.ShiftRightLogical(hi, inv_shift)};

    const IR::U32 shift_complement{ir.ISub(ir.Imm32(32), shift)};
    const IR::U32 short_hi_extract{ir.BitFieldExtract(hi, ir.Imm32(0), shift)};
    const IR::U32 short_ret_hi{shifted_hi};
    const IR::U32 short_ret_lo{
        ir.BitFieldInsert(shifted_lo, short_hi_extract, shift_complement, shift)};

    const IR::U32 zero_ret_lo{lo};
    const IR::U32 zero_ret_hi{hi};

    const IR::U32 non_zero_lo{ir.Select(is_long, long_ret_lo, short_ret_lo)};
    const IR::U32 non_zero_hi{ir.Select(is_long, long_ret_hi, short_ret_hi)};

    const IR::U32 ret_lo{ir.Select(is_zero, zero_ret_lo, non_zero_lo)};
    const IR::U32 ret_hi{ir.Select(is_zero, zero_ret_hi, non_zero_hi)};
    inst.ReplaceUsesWith(ir.CompositeConstruct(ret_lo, ret_hi));
}

void ShiftRightArithmetic64To32(IR::Block& block, IR::Inst& inst) {
    if (inst.HasAssociatedPseudoOperation()) {
        throw NotImplementedException("ShiftRightArithmetic64 emulation with pseudo instructions");
    }
    IR::IREmitter ir(block, IR::Block::InstructionList::s_iterator_to(inst));
    const auto [lo, hi]{Unpack(ir, inst.Arg(0))};
    const IR::U32 shift{inst.Arg(1)};

    const IR::U32 shifted_lo{ir.ShiftRightLogical(lo, shift)};
    const IR::U32 shifted_hi{ir.ShiftRightArithmetic(hi, shift)};

    const IR::U32 sign_extension{ir.ShiftRightArithmetic(hi, ir.Imm32(31))};

    const IR::U32 inv_shift{ir.ISub(shift, ir.Imm32(32))};
    const IR::U1 is_long{ir.IGreaterThanEqual(inv_shift, ir.Imm32(0), true)};
    const IR::U1 is_zero{ir.IEqual(shift, ir.Imm32(0))};

    const IR::U32 long_ret_hi{sign_extension};
    const IR::U32 long_ret_lo{ir.ShiftRightArithmetic(hi, inv_shift)};

    const IR::U32 shift_complement{ir.ISub(ir.Imm32(32), shift)};
    const IR::U32 short_hi_extract(ir.BitFieldExtract(hi, ir.Imm32(0), shift));
    const IR::U32 short_ret_hi{shifted_hi};
    const IR::U32 short_ret_lo{
        ir.BitFieldInsert(shifted_lo, short_hi_extract, shift_complement, shift)};

    const IR::U32 zero_ret_lo{lo};
    const IR::U32 zero_ret_hi{hi};

    const IR::U32 non_zero_lo{ir.Select(is_long, long_ret_lo, short_ret_lo)};
    const IR::U32 non_zero_hi{ir.Select(is_long, long_ret_hi, short_ret_hi)};

    const IR::U32 ret_lo{ir.Select(is_zero, zero_ret_lo, non_zero_lo)};
    const IR::U32 ret_hi{ir.Select(is_zero, zero_ret_hi, non_zero_hi)};
    inst.ReplaceUsesWith(ir.CompositeConstruct(ret_lo, ret_hi));
}

void ConvertF16U64To32(IR::Block& block, IR::Inst& inst) {
    IR::IREmitter ir(block, IR::Block::InstructionList::s_iterator_to(inst));
    const auto value_pair = Unpack(ir, inst.Arg(0));
    // Convert low 32-bits to F16, high bits ignored
    const IR::F16 result = ir.ConvertUToF(16, 32, value_pair.first);
    inst.ReplaceUsesWith(result);
}

void ConvertF32U64To32(IR::Block& block, IR::Inst& inst) {
    IR::IREmitter ir(block, IR::Block::InstructionList::s_iterator_to(inst));
    const auto value_pair = Unpack(ir, inst.Arg(0));
    // Convert low 32-bits to F32, high bits ignored
    const IR::F32 result = ir.ConvertUToF(32, 32, value_pair.first);
    inst.ReplaceUsesWith(result);
}

void ConvertF64U64To32(IR::Block& block, IR::Inst& inst) {
    IR::IREmitter ir(block, IR::Block::InstructionList::s_iterator_to(inst));
    const auto value_pair = Unpack(ir, inst.Arg(0));
    // Convert low 32-bits to F64, high bits ignored
    const IR::F64 result = ir.ConvertUToF(64, 32, value_pair.first);
    inst.ReplaceUsesWith(result);
}

void ConvertF16S64To32(IR::Block& block, IR::Inst& inst) {
    IR::IREmitter ir(block, IR::Block::InstructionList::s_iterator_to(inst));
    const auto value_pair = Unpack(ir, inst.Arg(0));
    // Convert low 32-bits to F16 as signed, high bits ignored
    const IR::F16 result = ir.ConvertSToF(16, 32, value_pair.first);
    inst.ReplaceUsesWith(result);
}

void ConvertF32S64To32(IR::Block& block, IR::Inst& inst) {
    IR::IREmitter ir(block, IR::Block::InstructionList::s_iterator_to(inst));
    const auto value_pair = Unpack(ir, inst.Arg(0));
    // Convert low 32-bits to F32 as signed, high bits ignored
    const IR::F32 result = ir.ConvertSToF(32, 32, value_pair.first);
    inst.ReplaceUsesWith(result);
}

void ConvertF64S64To32(IR::Block& block, IR::Inst& inst) {
    IR::IREmitter ir(block, IR::Block::InstructionList::s_iterator_to(inst));
    const auto value_pair = Unpack(ir, inst.Arg(0));
    // Convert low 32-bits to F64 as signed, high bits ignored
    const IR::F64 result = ir.ConvertSToF(64, 32, value_pair.first);
    inst.ReplaceUsesWith(result);
}

void ConvertU64U32To32(IR::Block& block, IR::Inst& inst) {
    IR::IREmitter ir(block, IR::Block::InstructionList::s_iterator_to(inst));
    const IR::U32 value{inst.Arg(0)};
    // U32 to U64: zero-extend to U32x2
    const IR::Value result = ir.CompositeConstruct(value, ir.Imm32(0));
    inst.ReplaceUsesWith(result);
}

void ConvertU32U64To32(IR::Block& block, IR::Inst& inst) {
    IR::IREmitter ir(block, IR::Block::InstructionList::s_iterator_to(inst));
    const auto value_pair = Unpack(ir, inst.Arg(0));
    // U64 to U32: take low 32-bits
    inst.ReplaceUsesWith(value_pair.first);
}

void ConvertS64FTo32(IR::Block& block, IR::Inst& inst) {
    // Float to S64: convert to S32 and sign-extend
    IR::IREmitter ir(block, IR::Block::InstructionList::s_iterator_to(inst));
    const IR::F16F32F64 value{inst.Arg(0)};
    const IR::U32 low = ir.ConvertFToS(32, value);
    const IR::U32 high = ir.ShiftRightArithmetic(low, ir.Imm32(31)); // Sign extend
    inst.ReplaceUsesWith(ir.CompositeConstruct(low, high));
}

void ConvertU64FTo32(IR::Block& block, IR::Inst& inst) {
    // Float to U64: convert to U32 and zero-extend
    IR::IREmitter ir(block, IR::Block::InstructionList::s_iterator_to(inst));
    const IR::F16F32F64 value{inst.Arg(0)};
    const IR::U32 low = ir.ConvertFToU(32, value);
    const IR::U32 high = ir.Imm32(0); // Zero extend
    inst.ReplaceUsesWith(ir.CompositeConstruct(low, high));
}

void Lower(IR::Block& block, IR::Inst& inst) {
    switch (inst.GetOpcode()) {
    case IR::Opcode::PackUint2x32:
    case IR::Opcode::UnpackUint2x32:
        return inst.ReplaceOpcode(IR::Opcode::Identity);
    // Conversion operations
    case IR::Opcode::ConvertF16U64:
        return ConvertF16U64To32(block, inst);
    case IR::Opcode::ConvertF32U64:
        return ConvertF32U64To32(block, inst);
    case IR::Opcode::ConvertF64U64:
        return ConvertF64U64To32(block, inst);
    case IR::Opcode::ConvertF16S64:
        return ConvertF16S64To32(block, inst);
    case IR::Opcode::ConvertF32S64:
        return ConvertF32S64To32(block, inst);
    case IR::Opcode::ConvertF64S64:
        return ConvertF64S64To32(block, inst);
    case IR::Opcode::ConvertU64U32:
        return ConvertU64U32To32(block, inst);
    case IR::Opcode::ConvertU32U64:
        return ConvertU32U64To32(block, inst);
    case IR::Opcode::ConvertS64F16:
    case IR::Opcode::ConvertS64F32:
    case IR::Opcode::ConvertS64F64:
        return ConvertS64FTo32(block, inst);
    case IR::Opcode::ConvertU64F16:
    case IR::Opcode::ConvertU64F32:
    case IR::Opcode::ConvertU64F64:
        return ConvertU64FTo32(block, inst);
    // Arithmetic operations
    case IR::Opcode::IAdd64:
        return IAdd64To32(block, inst);
    case IR::Opcode::ISub64:
        return ISub64To32(block, inst);
    case IR::Opcode::INeg64:
        return INeg64To32(block, inst);
    case IR::Opcode::ShiftLeftLogical64:
        return ShiftLeftLogical64To32(block, inst);
    case IR::Opcode::ShiftRightLogical64:
        return ShiftRightLogical64To32(block, inst);
    case IR::Opcode::ShiftRightArithmetic64:
        return ShiftRightArithmetic64To32(block, inst);
    // Atomic operations
    case IR::Opcode::SharedAtomicExchange64:
        return inst.ReplaceOpcode(IR::Opcode::SharedAtomicExchange32x2);
    case IR::Opcode::GlobalAtomicIAdd64:
        return inst.ReplaceOpcode(IR::Opcode::GlobalAtomicIAdd32x2);
    case IR::Opcode::GlobalAtomicSMin64:
        return inst.ReplaceOpcode(IR::Opcode::GlobalAtomicSMin32x2);
    case IR::Opcode::GlobalAtomicUMin64:
        return inst.ReplaceOpcode(IR::Opcode::GlobalAtomicUMin32x2);
    case IR::Opcode::GlobalAtomicSMax64:
        return inst.ReplaceOpcode(IR::Opcode::GlobalAtomicSMax32x2);
    case IR::Opcode::GlobalAtomicUMax64:
        return inst.ReplaceOpcode(IR::Opcode::GlobalAtomicUMax32x2);
    case IR::Opcode::GlobalAtomicAnd64:
        return inst.ReplaceOpcode(IR::Opcode::GlobalAtomicAnd32x2);
    case IR::Opcode::GlobalAtomicOr64:
        return inst.ReplaceOpcode(IR::Opcode::GlobalAtomicOr32x2);
    case IR::Opcode::GlobalAtomicXor64:
        return inst.ReplaceOpcode(IR::Opcode::GlobalAtomicXor32x2);
    case IR::Opcode::GlobalAtomicExchange64:
        return inst.ReplaceOpcode(IR::Opcode::GlobalAtomicExchange32x2);
    default:
        break;
    }
}
} // Anonymous namespace

void LowerInt64ToInt32(IR::Program& program) {
    const auto end{program.post_order_blocks.rend()};
    for (auto it = program.post_order_blocks.rbegin(); it != end; ++it) {
        IR::Block* const block{*it};
        for (IR::Inst& inst : block->Instructions()) {
            Lower(*block, inst);
        }
    }
}

} // namespace Shader::Optimization
