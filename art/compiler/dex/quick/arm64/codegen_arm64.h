/*
 * Copyright (C) 2011 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef ART_COMPILER_DEX_QUICK_ARM64_CODEGEN_ARM64_H_
#define ART_COMPILER_DEX_QUICK_ARM64_CODEGEN_ARM64_H_

#include "arm64_lir.h"
#include "dex/compiler_internals.h"

#include <map>

namespace art {

class Arm64Mir2Lir FINAL : public Mir2Lir {
 protected:
  // TODO: consolidate 64-bit target support.
  class InToRegStorageMapper {
   public:
    virtual RegStorage GetNextReg(bool is_double_or_float, bool is_wide, bool is_ref) = 0;
    virtual ~InToRegStorageMapper() {}
  };

  class InToRegStorageArm64Mapper : public InToRegStorageMapper {
   public:
    InToRegStorageArm64Mapper() : cur_core_reg_(0), cur_fp_reg_(0) {}
    virtual ~InToRegStorageArm64Mapper() {}
    virtual RegStorage GetNextReg(bool is_double_or_float, bool is_wide, bool is_ref);
   private:
    int cur_core_reg_;
    int cur_fp_reg_;
  };

  class InToRegStorageMapping {
   public:
    InToRegStorageMapping() : max_mapped_in_(0), is_there_stack_mapped_(false),
    initialized_(false) {}
    void Initialize(RegLocation* arg_locs, int count, InToRegStorageMapper* mapper);
    int GetMaxMappedIn() { return max_mapped_in_; }
    bool IsThereStackMapped() { return is_there_stack_mapped_; }
    RegStorage Get(int in_position);
    bool IsInitialized() { return initialized_; }
   private:
    std::map<int, RegStorage> mapping_;
    int max_mapped_in_;
    bool is_there_stack_mapped_;
    bool initialized_;
  };

  public:
    Arm64Mir2Lir(CompilationUnit* cu, MIRGraph* mir_graph, ArenaAllocator* arena);

    // Required for target - codegen helpers.
    bool SmallLiteralDivRem(Instruction::Code dalvik_opcode, bool is_div, RegLocation rl_src,
                            RegLocation rl_dest, int lit) OVERRIDE;
    bool HandleEasyDivRem(Instruction::Code dalvik_opcode, bool is_div,
                          RegLocation rl_src, RegLocation rl_dest, int lit) OVERRIDE;
    bool EasyMultiply(RegLocation rl_src, RegLocation rl_dest, int lit) OVERRIDE;
    LIR* CheckSuspendUsingLoad() OVERRIDE;
    RegStorage LoadHelper(ThreadOffset<4> offset) OVERRIDE;
    RegStorage LoadHelper(ThreadOffset<8> offset) OVERRIDE;
    LIR* LoadBaseDisp(RegStorage r_base, int displacement, RegStorage r_dest,
                      OpSize size, VolatileKind is_volatile) OVERRIDE;
    LIR* LoadRefDisp(RegStorage r_base, int displacement, RegStorage r_dest,
                     VolatileKind is_volatile)
        OVERRIDE;
    LIR* LoadBaseIndexed(RegStorage r_base, RegStorage r_index, RegStorage r_dest, int scale,
                         OpSize size) OVERRIDE;
    LIR* LoadRefIndexed(RegStorage r_base, RegStorage r_index, RegStorage r_dest) OVERRIDE;
    LIR* LoadBaseIndexedDisp(RegStorage r_base, RegStorage r_index, int scale, int displacement,
                             RegStorage r_dest, OpSize size) OVERRIDE;
    LIR* LoadConstantNoClobber(RegStorage r_dest, int value);
    LIR* LoadConstantWide(RegStorage r_dest, int64_t value);
    LIR* StoreBaseDisp(RegStorage r_base, int displacement, RegStorage r_src,
                       OpSize size, VolatileKind is_volatile) OVERRIDE;
    LIR* StoreRefDisp(RegStorage r_base, int displacement, RegStorage r_src,
                      VolatileKind is_volatile) OVERRIDE;
    LIR* StoreBaseIndexed(RegStorage r_base, RegStorage r_index, RegStorage r_src, int scale,
                          OpSize size) OVERRIDE;
    LIR* StoreRefIndexed(RegStorage r_base, RegStorage r_index, RegStorage r_src) OVERRIDE;
    LIR* StoreBaseIndexedDisp(RegStorage r_base, RegStorage r_index, int scale, int displacement,
                              RegStorage r_src, OpSize size) OVERRIDE;
    void MarkGCCard(RegStorage val_reg, RegStorage tgt_addr_reg) OVERRIDE;
    LIR* OpCmpMemImmBranch(ConditionCode cond, RegStorage temp_reg, RegStorage base_reg,
                           int offset, int check_value, LIR* target) OVERRIDE;

    // Required for target - register utilities.
    RegStorage TargetReg(SpecialTargetRegister reg) OVERRIDE;
    RegStorage TargetReg(SpecialTargetRegister symbolic_reg, bool is_wide) OVERRIDE {
      RegStorage reg = TargetReg(symbolic_reg);
      if (is_wide) {
        return (reg.Is64Bit()) ? reg : As64BitReg(reg);
      } else {
        return (reg.Is32Bit()) ? reg : As32BitReg(reg);
      }
    }
    RegStorage TargetRefReg(SpecialTargetRegister symbolic_reg) OVERRIDE {
      RegStorage reg = TargetReg(symbolic_reg);
      return (reg.Is64Bit() ? reg : As64BitReg(reg));
    }
    RegStorage TargetPtrReg(SpecialTargetRegister symbolic_reg) OVERRIDE {
      RegStorage reg = TargetReg(symbolic_reg);
      return (reg.Is64Bit() ? reg : As64BitReg(reg));
    }
    RegStorage GetArgMappingToPhysicalReg(int arg_num);
    RegLocation GetReturnAlt();
    RegLocation GetReturnWideAlt();
    RegLocation LocCReturn();
    RegLocation LocCReturnRef();
    RegLocation LocCReturnDouble();
    RegLocation LocCReturnFloat();
    RegLocation LocCReturnWide();
    ResourceMask GetRegMaskCommon(const RegStorage& reg) const OVERRIDE;
    void AdjustSpillMask();
    void ClobberCallerSave();
    void FreeCallTemps();
    void LockCallTemps();
    void CompilerInitializeRegAlloc();

    // Required for target - miscellaneous.
    void AssembleLIR();
    uint32_t LinkFixupInsns(LIR* head_lir, LIR* tail_lir, CodeOffset offset);
    int AssignInsnOffsets();
    void AssignOffsets();
    uint8_t* EncodeLIRs(uint8_t* write_pos, LIR* lir);
    void DumpResourceMask(LIR* lir, const ResourceMask& mask, const char* prefix) OVERRIDE;
    void SetupTargetResourceMasks(LIR* lir, uint64_t flags,
                                  ResourceMask* use_mask, ResourceMask* def_mask) OVERRIDE;
    const char* GetTargetInstFmt(int opcode);
    const char* GetTargetInstName(int opcode);
    std::string BuildInsnString(const char* fmt, LIR* lir, unsigned char* base_addr);
    ResourceMask GetPCUseDefEncoding() const OVERRIDE;
    uint64_t GetTargetInstFlags(int opcode);
    size_t GetInsnSize(LIR* lir) OVERRIDE;
    bool IsUnconditionalBranch(LIR* lir);

    // Check support for volatile load/store of a given size.
    bool SupportsVolatileLoadStore(OpSize size) OVERRIDE;
    // Get the register class for load/store of a field.
    RegisterClass RegClassForFieldLoadStore(OpSize size, bool is_volatile) OVERRIDE;

    // Required for target - Dalvik-level generators.
    void GenShiftOpLong(Instruction::Code opcode, RegLocation rl_dest, RegLocation rl_src1,
                        RegLocation lr_shift);
    void GenArithImmOpLong(Instruction::Code opcode, RegLocation rl_dest,
                           RegLocation rl_src1, RegLocation rl_src2);
    void GenArrayGet(int opt_flags, OpSize size, RegLocation rl_array,
                     RegLocation rl_index, RegLocation rl_dest, int scale);
    void GenArrayPut(int opt_flags, OpSize size, RegLocation rl_array, RegLocation rl_index,
                     RegLocation rl_src, int scale, bool card_mark);
    void GenShiftImmOpLong(Instruction::Code opcode, RegLocation rl_dest,
                           RegLocation rl_src1, RegLocation rl_shift);
    void GenLongOp(OpKind op, RegLocation rl_dest, RegLocation rl_src1, RegLocation rl_src2);
    void GenMulLong(Instruction::Code opcode, RegLocation rl_dest, RegLocation rl_src1,
                    RegLocation rl_src2);
    void GenAddLong(Instruction::Code opcode, RegLocation rl_dest, RegLocation rl_src1,
                    RegLocation rl_src2);
    void GenAndLong(Instruction::Code opcode, RegLocation rl_dest, RegLocation rl_src1,
                    RegLocation rl_src2);
    void GenArithOpDouble(Instruction::Code opcode, RegLocation rl_dest, RegLocation rl_src1,
                          RegLocation rl_src2);
    void GenArithOpFloat(Instruction::Code opcode, RegLocation rl_dest, RegLocation rl_src1,
                         RegLocation rl_src2);
    void GenCmpFP(Instruction::Code opcode, RegLocation rl_dest, RegLocation rl_src1,
                  RegLocation rl_src2);
    void GenConversion(Instruction::Code opcode, RegLocation rl_dest, RegLocation rl_src);
    bool GenInlinedReverseBits(CallInfo* info, OpSize size);
    bool GenInlinedAbsDouble(CallInfo* info) OVERRIDE;
    bool GenInlinedCas(CallInfo* info, bool is_long, bool is_object);
    bool GenInlinedMinMax(CallInfo* info, bool is_min, bool is_long);
    bool GenInlinedMinMaxFP(CallInfo* info, bool is_min, bool is_double);
    bool GenInlinedSqrt(CallInfo* info);
    bool GenInlinedPeek(CallInfo* info, OpSize size);
    bool GenInlinedPoke(CallInfo* info, OpSize size);
    bool GenInlinedAbsLong(CallInfo* info);
    void GenIntToLong(RegLocation rl_dest, RegLocation rl_src);
    void GenNotLong(RegLocation rl_dest, RegLocation rl_src);
    void GenNegLong(RegLocation rl_dest, RegLocation rl_src);
    void GenOrLong(Instruction::Code opcode, RegLocation rl_dest, RegLocation rl_src1,
                   RegLocation rl_src2);
    void GenSubLong(Instruction::Code opcode, RegLocation rl_dest, RegLocation rl_src1,
                    RegLocation rl_src2);
    void GenXorLong(Instruction::Code opcode, RegLocation rl_dest, RegLocation rl_src1,
                    RegLocation rl_src2);
    void GenDivRemLong(Instruction::Code opcode, RegLocation rl_dest, RegLocation rl_src1,
                       RegLocation rl_src2, bool is_div);
    RegLocation GenDivRem(RegLocation rl_dest, RegStorage reg_lo, RegStorage reg_hi, bool is_div);
    RegLocation GenDivRemLit(RegLocation rl_dest, RegStorage reg_lo, int lit, bool is_div);
    void GenCmpLong(RegLocation rl_dest, RegLocation rl_src1, RegLocation rl_src2);
    void GenDivZeroCheckWide(RegStorage reg);
    void GenEntrySequence(RegLocation* ArgLocs, RegLocation rl_method);
    void GenExitSequence();
    void GenSpecialExitSequence();
    void GenFillArrayData(DexOffset table_offset, RegLocation rl_src);
    void GenFusedFPCmpBranch(BasicBlock* bb, MIR* mir, bool gt_bias, bool is_double);
    void GenFusedLongCmpBranch(BasicBlock* bb, MIR* mir);
    void GenSelect(BasicBlock* bb, MIR* mir);
    bool GenMemBarrier(MemBarrierKind barrier_kind);
    void GenMonitorEnter(int opt_flags, RegLocation rl_src);
    void GenMonitorExit(int opt_flags, RegLocation rl_src);
    void GenMoveException(RegLocation rl_dest);
    void GenMultiplyByTwoBitMultiplier(RegLocation rl_src, RegLocation rl_result, int lit,
                                       int first_bit, int second_bit);
    void GenNegDouble(RegLocation rl_dest, RegLocation rl_src);
    void GenNegFloat(RegLocation rl_dest, RegLocation rl_src);
    void GenPackedSwitch(MIR* mir, DexOffset table_offset, RegLocation rl_src);
    void GenSparseSwitch(MIR* mir, DexOffset table_offset, RegLocation rl_src);

    uint32_t GenPairWise(uint32_t reg_mask, int* reg1, int* reg2);
    void UnSpillCoreRegs(RegStorage base, int offset, uint32_t reg_mask);
    void SpillCoreRegs(RegStorage base, int offset, uint32_t reg_mask);
    void UnSpillFPRegs(RegStorage base, int offset, uint32_t reg_mask);
    void SpillFPRegs(RegStorage base, int offset, uint32_t reg_mask);

    // Required for target - single operation generators.
    LIR* OpUnconditionalBranch(LIR* target);
    LIR* OpCmpBranch(ConditionCode cond, RegStorage src1, RegStorage src2, LIR* target);
    LIR* OpCmpImmBranch(ConditionCode cond, RegStorage reg, int check_value, LIR* target);
    LIR* OpCondBranch(ConditionCode cc, LIR* target);
    LIR* OpDecAndBranch(ConditionCode c_code, RegStorage reg, LIR* target);
    LIR* OpFpRegCopy(RegStorage r_dest, RegStorage r_src);
    LIR* OpIT(ConditionCode cond, const char* guide);
    void OpEndIT(LIR* it);
    LIR* OpMem(OpKind op, RegStorage r_base, int disp);
    LIR* OpPcRelLoad(RegStorage reg, LIR* target);
    LIR* OpReg(OpKind op, RegStorage r_dest_src);
    void OpRegCopy(RegStorage r_dest, RegStorage r_src);
    LIR* OpRegCopyNoInsert(RegStorage r_dest, RegStorage r_src);
    LIR* OpRegImm64(OpKind op, RegStorage r_dest_src1, int64_t value);
    LIR* OpRegImm(OpKind op, RegStorage r_dest_src1, int value);
    LIR* OpRegMem(OpKind op, RegStorage r_dest, RegStorage r_base, int offset);
    LIR* OpRegReg(OpKind op, RegStorage r_dest_src1, RegStorage r_src2);
    LIR* OpMovRegMem(RegStorage r_dest, RegStorage r_base, int offset, MoveType move_type);
    LIR* OpMovMemReg(RegStorage r_base, int offset, RegStorage r_src, MoveType move_type);
    LIR* OpCondRegReg(OpKind op, ConditionCode cc, RegStorage r_dest, RegStorage r_src);
    LIR* OpRegRegImm64(OpKind op, RegStorage r_dest, RegStorage r_src1, int64_t value);
    LIR* OpRegRegImm(OpKind op, RegStorage r_dest, RegStorage r_src1, int value);
    LIR* OpRegRegReg(OpKind op, RegStorage r_dest, RegStorage r_src1, RegStorage r_src2);
    LIR* OpTestSuspend(LIR* target);
    LIR* OpThreadMem(OpKind op, ThreadOffset<4> thread_offset) OVERRIDE;
    LIR* OpThreadMem(OpKind op, ThreadOffset<8> thread_offset) OVERRIDE;
    LIR* OpVldm(RegStorage r_base, int count);
    LIR* OpVstm(RegStorage r_base, int count);
    void OpLea(RegStorage r_base, RegStorage reg1, RegStorage reg2, int scale, int offset);
    void OpRegCopyWide(RegStorage dest, RegStorage src);
    void OpTlsCmp(ThreadOffset<4> offset, int val) OVERRIDE;
    void OpTlsCmp(ThreadOffset<8> offset, int val) OVERRIDE;

    LIR* LoadBaseDispBody(RegStorage r_base, int displacement, RegStorage r_dest, OpSize size);
    LIR* StoreBaseDispBody(RegStorage r_base, int displacement, RegStorage r_src, OpSize size);
    LIR* OpRegRegRegShift(OpKind op, RegStorage r_dest, RegStorage r_src1, RegStorage r_src2,
                          int shift);
    LIR* OpRegRegRegExtend(OpKind op, RegStorage r_dest, RegStorage r_src1, RegStorage r_src2,
                           A64RegExtEncodings ext, uint8_t amount);
    LIR* OpRegRegShift(OpKind op, RegStorage r_dest_src1, RegStorage r_src2, int shift);
    LIR* OpRegRegExtend(OpKind op, RegStorage r_dest_src1, RegStorage r_src2, int shift);
    static const ArmEncodingMap EncodingMap[kA64Last];
    int EncodeShift(int code, int amount);
    int EncodeExtend(int extend_type, int amount);
    bool IsExtendEncoding(int encoded_value);
    int EncodeLogicalImmediate(bool is_wide, uint64_t value);
    uint64_t DecodeLogicalImmediate(bool is_wide, int value);

    ArmConditionCode ArmConditionEncoding(ConditionCode code);
    bool InexpensiveConstantInt(int32_t value);
    bool InexpensiveConstantFloat(int32_t value);
    bool InexpensiveConstantLong(int64_t value);
    bool InexpensiveConstantDouble(int64_t value);

    void FlushIns(RegLocation* ArgLocs, RegLocation rl_method);

    int GenDalvikArgsNoRange(CallInfo* info, int call_state, LIR** pcrLabel,
                             NextCallInsn next_call_insn,
                             const MethodReference& target_method,
                             uint32_t vtable_idx,
                             uintptr_t direct_code, uintptr_t direct_method, InvokeType type,
                             bool skip_this);

    int GenDalvikArgsRange(CallInfo* info, int call_state, LIR** pcrLabel,
                           NextCallInsn next_call_insn,
                           const MethodReference& target_method,
                           uint32_t vtable_idx,
                           uintptr_t direct_code, uintptr_t direct_method, InvokeType type,
                           bool skip_this);
    InToRegStorageMapping in_to_reg_storage_mapping_;

  private:
    /**
     * @brief Given register xNN (dNN), returns register wNN (sNN).
     * @param reg #RegStorage containing a Solo64 input register (e.g. @c x1 or @c d2).
     * @return A Solo32 with the same register number as the @p reg (e.g. @c w1 or @c s2).
     * @see As64BitReg
     */
    RegStorage As32BitReg(RegStorage reg) {
      DCHECK(!reg.IsPair());
      if ((kFailOnSizeError || kReportSizeError) && !reg.Is64Bit()) {
        if (kFailOnSizeError) {
          LOG(FATAL) << "Expected 64b register";
        } else {
          LOG(WARNING) << "Expected 64b register";
          return reg;
        }
      }
      RegStorage ret_val = RegStorage(RegStorage::k32BitSolo,
                                      reg.GetRawBits() & RegStorage::kRegTypeMask);
      DCHECK_EQ(GetRegInfo(reg)->FindMatchingView(RegisterInfo::k32SoloStorageMask)
                               ->GetReg().GetReg(),
                ret_val.GetReg());
      return ret_val;
    }

    RegStorage Check32BitReg(RegStorage reg) {
      if ((kFailOnSizeError || kReportSizeError) && !reg.Is32Bit()) {
        if (kFailOnSizeError) {
          LOG(FATAL) << "Checked for 32b register";
        } else {
          LOG(WARNING) << "Checked for 32b register";
          return As32BitReg(reg);
        }
      }
      return reg;
    }

    /**
     * @brief Given register wNN (sNN), returns register xNN (dNN).
     * @param reg #RegStorage containing a Solo32 input register (e.g. @c w1 or @c s2).
     * @return A Solo64 with the same register number as the @p reg (e.g. @c x1 or @c d2).
     * @see As32BitReg
     */
    RegStorage As64BitReg(RegStorage reg) {
      DCHECK(!reg.IsPair());
      if ((kFailOnSizeError || kReportSizeError) && !reg.Is32Bit()) {
        if (kFailOnSizeError) {
          LOG(FATAL) << "Expected 32b register";
        } else {
          LOG(WARNING) << "Expected 32b register";
          return reg;
        }
      }
      RegStorage ret_val = RegStorage(RegStorage::k64BitSolo,
                                      reg.GetRawBits() & RegStorage::kRegTypeMask);
      DCHECK_EQ(GetRegInfo(reg)->FindMatchingView(RegisterInfo::k64SoloStorageMask)
                               ->GetReg().GetReg(),
                ret_val.GetReg());
      return ret_val;
    }

    RegStorage Check64BitReg(RegStorage reg) {
      if ((kFailOnSizeError || kReportSizeError) && !reg.Is64Bit()) {
        if (kFailOnSizeError) {
          LOG(FATAL) << "Checked for 64b register";
        } else {
          LOG(WARNING) << "Checked for 64b register";
          return As64BitReg(reg);
        }
      }
      return reg;
    }

    LIR* LoadFPConstantValue(RegStorage r_dest, int32_t value);
    LIR* LoadFPConstantValueWide(RegStorage r_dest, int64_t value);
    void ReplaceFixup(LIR* prev_lir, LIR* orig_lir, LIR* new_lir);
    void InsertFixupBefore(LIR* prev_lir, LIR* orig_lir, LIR* new_lir);
    void AssignDataOffsets();
    RegLocation GenDivRem(RegLocation rl_dest, RegLocation rl_src1, RegLocation rl_src2,
                          bool is_div, bool check_zero);
    RegLocation GenDivRemLit(RegLocation rl_dest, RegLocation rl_src1, int lit, bool is_div);
};

}  // namespace art

#endif  // ART_COMPILER_DEX_QUICK_ARM64_CODEGEN_ARM64_H_
