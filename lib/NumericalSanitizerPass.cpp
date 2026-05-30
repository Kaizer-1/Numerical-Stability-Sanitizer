#include "NumericalSanitizer/NumericalSanitizer.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassBuilder.h"
#if __has_include("llvm/Plugins/PassPlugin.h")
#include "llvm/Plugins/PassPlugin.h"
#else
#include "llvm/Passes/PassPlugin.h"
#endif

#include <array>
#include <functional>

using namespace llvm;

namespace {

static bool startsWith(StringRef Text, StringRef Prefix) {
#if LLVM_VERSION_MAJOR >= 22
  return Text.starts_with(Prefix);
#else
  return Text.startswith(Prefix);
#endif
}

struct RuntimeDecls {
  FunctionCallee ShadowLoad;
  FunctionCallee ShadowStore;
  FunctionCallee CopyShadowBytes;
  FunctionCallee MoveShadowBytes;
  FunctionCallee ForgetShadowBytes;
  FunctionCallee SetArgShadow;
  FunctionCallee GetArgShadow;
  FunctionCallee ClearArgShadows;
  FunctionCallee SetReturnShadow;
  FunctionCallee GetReturnShadow;
  FunctionCallee ShadowSqrtf;
  FunctionCallee ShadowSinf;
  FunctionCallee ShadowCosf;
  FunctionCallee ShadowTanf;
  FunctionCallee ShadowExpf;
  FunctionCallee ShadowLogf;
  FunctionCallee ShadowPowf;
  FunctionCallee ShadowFabsf;
  FunctionCallee ShadowFloorf;
  FunctionCallee ShadowCeilf;
  FunctionCallee ShadowFmodf;
  FunctionCallee ShadowAtanf;
  FunctionCallee ShadowAtan2f;
  FunctionCallee ShadowAsinf;
  FunctionCallee ShadowAcosf;
  FunctionCallee ShadowSinhf;
  FunctionCallee ShadowCoshf;
  FunctionCallee Check;
  FunctionCallee CheckBinary;
};

struct NumericalSanitizerPass : public PassInfoMixin<NumericalSanitizerPass> {
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &) {
    if (F.isDeclaration() || startsWith(F.getName(), "__nsan_"))
      return PreservedAnalyses::all();

    Module &M = *F.getParent();
    const DataLayout &DL = M.getDataLayout();
    LLVMContext &Ctx = M.getContext();
    Type *FloatTy = Type::getFloatTy(Ctx);
    Type *DoubleTy = Type::getDoubleTy(Ctx);
    Type *VoidTy = Type::getVoidTy(Ctx);
    Type *I8PtrTy = PointerType::getUnqual(Ctx);
    Type *I32Ty = Type::getInt32Ty(Ctx);
    Type *I64Ty = Type::getInt64Ty(Ctx);

    RuntimeDecls RT;
    RT.ShadowLoad = M.getOrInsertFunction("__nsan_shadow_load_float", DoubleTy,
                                          I8PtrTy, FloatTy);
    RT.ShadowStore = M.getOrInsertFunction("__nsan_shadow_store_float", VoidTy,
                                           I8PtrTy, DoubleTy);
    RT.CopyShadowBytes = M.getOrInsertFunction("__nsan_copy_shadow_bytes", VoidTy,
                                               I8PtrTy, I8PtrTy, I64Ty);
    RT.MoveShadowBytes = M.getOrInsertFunction("__nsan_move_shadow_bytes", VoidTy,
                                               I8PtrTy, I8PtrTy, I64Ty);
    RT.ForgetShadowBytes =
        M.getOrInsertFunction("__nsan_forget_shadow_bytes", VoidTy, I8PtrTy,
                              I64Ty);
    RT.SetArgShadow = M.getOrInsertFunction("__nsan_set_arg_shadow", VoidTy,
                                            I32Ty, DoubleTy);
    RT.GetArgShadow = M.getOrInsertFunction("__nsan_get_arg_shadow", DoubleTy,
                                            I32Ty, FloatTy);
    RT.ClearArgShadows =
        M.getOrInsertFunction("__nsan_clear_arg_shadows", VoidTy, I32Ty);
    RT.SetReturnShadow =
        M.getOrInsertFunction("__nsan_set_return_shadow", VoidTy, DoubleTy);
    RT.GetReturnShadow =
        M.getOrInsertFunction("__nsan_get_return_shadow", DoubleTy, FloatTy);
    RT.ShadowSqrtf =
        M.getOrInsertFunction("__nsan_shadow_sqrtf", DoubleTy, FloatTy, DoubleTy);
    RT.ShadowSinf =
        M.getOrInsertFunction("__nsan_shadow_sinf", DoubleTy, FloatTy, DoubleTy);
    RT.ShadowCosf =
        M.getOrInsertFunction("__nsan_shadow_cosf", DoubleTy, FloatTy, DoubleTy);
    RT.ShadowTanf =
        M.getOrInsertFunction("__nsan_shadow_tanf", DoubleTy, FloatTy, DoubleTy);
    RT.ShadowExpf =
        M.getOrInsertFunction("__nsan_shadow_expf", DoubleTy, FloatTy, DoubleTy);
    RT.ShadowLogf =
        M.getOrInsertFunction("__nsan_shadow_logf", DoubleTy, FloatTy, DoubleTy);
    RT.ShadowPowf = M.getOrInsertFunction("__nsan_shadow_powf", DoubleTy,
                                          FloatTy, DoubleTy, DoubleTy);
    RT.ShadowFabsf =
        M.getOrInsertFunction("__nsan_shadow_fabsf", DoubleTy, FloatTy, DoubleTy);
    RT.ShadowFloorf = M.getOrInsertFunction("__nsan_shadow_floorf", DoubleTy,
                                            FloatTy, DoubleTy);
    RT.ShadowCeilf =
        M.getOrInsertFunction("__nsan_shadow_ceilf", DoubleTy, FloatTy, DoubleTy);
    RT.ShadowFmodf = M.getOrInsertFunction("__nsan_shadow_fmodf", DoubleTy,
                                           FloatTy, DoubleTy, DoubleTy);
    RT.ShadowAtanf =
        M.getOrInsertFunction("__nsan_shadow_atanf", DoubleTy, FloatTy, DoubleTy);
    RT.ShadowAtan2f = M.getOrInsertFunction("__nsan_shadow_atan2f", DoubleTy,
                                            FloatTy, DoubleTy, DoubleTy);
    RT.ShadowAsinf =
        M.getOrInsertFunction("__nsan_shadow_asinf", DoubleTy, FloatTy, DoubleTy);
    RT.ShadowAcosf =
        M.getOrInsertFunction("__nsan_shadow_acosf", DoubleTy, FloatTy, DoubleTy);
    RT.ShadowSinhf =
        M.getOrInsertFunction("__nsan_shadow_sinhf", DoubleTy, FloatTy, DoubleTy);
    RT.ShadowCoshf =
        M.getOrInsertFunction("__nsan_shadow_coshf", DoubleTy, FloatTy, DoubleTy);
    RT.Check = M.getOrInsertFunction("__nsan_check_float", VoidTy, FloatTy,
                                     DoubleTy, I32Ty, I8PtrTy, I8PtrTy, I32Ty,
                                     I32Ty);
    RT.CheckBinary = M.getOrInsertFunction(
        "__nsan_check_binary_float", VoidTy, FloatTy, DoubleTy, DoubleTy,
        DoubleTy, I32Ty, I8PtrTy, I8PtrTy, I32Ty, I32Ty);

    DenseMap<Value *, Value *> Shadow;
    bool Changed = false;

    auto floatVectorType = [](Type *Ty) -> FixedVectorType * {
      auto *FVT = dyn_cast<FixedVectorType>(Ty);
      if (!FVT || !FVT->getElementType()->isFloatTy())
        return nullptr;
      return FVT;
    };

    auto doubleVectorType = [](Type *Ty) -> FixedVectorType * {
      auto *FVT = dyn_cast<FixedVectorType>(Ty);
      if (!FVT || !FVT->getElementType()->isDoubleTy())
        return nullptr;
      return FVT;
    };

    auto isFloatValue = [&](Value *V) {
      return V && (V->getType()->isFloatTy() || floatVectorType(V->getType()));
    };

    auto shadowType = [&](Type *Ty) -> Type * {
      if (Ty->isFloatTy())
        return DoubleTy;
      if (auto *FVT = floatVectorType(Ty))
        return FixedVectorType::get(DoubleTy, FVT->getNumElements());
      return nullptr;
    };

    auto constDouble = [&](double V) { return ConstantFP::get(DoubleTy, V); };

    std::function<Value *(Value *, Instruction *)> getShadow =
        [&](Value *V, Instruction *InsertBefore) -> Value * {
      if (!isFloatValue(V))
        return nullptr;
      if (Value *Known = Shadow.lookup(V))
        return Known;

      Type *ShadowTy = shadowType(V->getType());
      if (!ShadowTy)
        return nullptr;

      if (auto *CFP = dyn_cast<ConstantFP>(V)) {
        double AsDouble = CFP->getValueAPF().convertToDouble();
        Value *C = constDouble(AsDouble);
        Shadow[V] = C;
        return C;
      }

      if (isa<UndefValue>(V) || isa<PoisonValue>(V))
        return UndefValue::get(ShadowTy);

      IRBuilder<> B(InsertBefore);
      Value *Extended = B.CreateFPExt(V, ShadowTy, V->getName() + ".nsan");
      Shadow[V] = Extended;
      return Extended;
    };

    auto ptrAsI8 = [&](IRBuilder<> &B, Value *Ptr) -> Value * {
      return B.CreatePointerCast(Ptr, I8PtrTy);
    };

    auto sizeAsI64 = [&](IRBuilder<> &B, Value *SizeV) -> Value * {
      if (SizeV->getType() == I64Ty)
        return SizeV;
      if (SizeV->getType()->isIntegerTy())
        return B.CreateZExtOrTrunc(SizeV, I64Ty);
      return ConstantInt::get(I64Ty, 0);
    };

    auto typeStoreSize = [&](Type *Ty) -> uint64_t {
      return Ty->isSized() ? DL.getTypeStoreSize(Ty) : 0;
    };

    auto directCalleeName = [&](CallBase &Call) -> StringRef {
      Function *Callee = Call.getCalledFunction();
      return Callee ? Callee->getName() : StringRef();
    };

    auto mathShadowCallee = [&](StringRef Name, unsigned FloatArgCount)
        -> FunctionCallee {
      if (FloatArgCount == 1) {
        if (Name == "sqrtf" || Name == "llvm.sqrt.f32")
          return RT.ShadowSqrtf;
        if (Name == "sinf" || Name == "llvm.sin.f32")
          return RT.ShadowSinf;
        if (Name == "cosf" || Name == "llvm.cos.f32")
          return RT.ShadowCosf;
        if (Name == "tanf")
          return RT.ShadowTanf;
        if (Name == "expf" || Name == "llvm.exp.f32")
          return RT.ShadowExpf;
        if (Name == "logf" || Name == "llvm.log.f32")
          return RT.ShadowLogf;
        if (Name == "fabsf" || Name == "llvm.fabs.f32")
          return RT.ShadowFabsf;
        if (Name == "floorf" || Name == "llvm.floor.f32")
          return RT.ShadowFloorf;
        if (Name == "ceilf" || Name == "llvm.ceil.f32")
          return RT.ShadowCeilf;
        if (Name == "atanf")
          return RT.ShadowAtanf;
        if (Name == "asinf")
          return RT.ShadowAsinf;
        if (Name == "acosf")
          return RT.ShadowAcosf;
        if (Name == "sinhf")
          return RT.ShadowSinhf;
        if (Name == "coshf")
          return RT.ShadowCoshf;
      }
      if (FloatArgCount == 2 && Name == "powf")
        return RT.ShadowPowf;
      if (FloatArgCount == 2 && Name == "fmodf")
        return RT.ShadowFmodf;
      if (FloatArgCount == 2 && Name == "atan2f")
        return RT.ShadowAtan2f;
      return FunctionCallee();
    };

    auto debugValues = [&](IRBuilder<> &B, Instruction &I) {
      StringRef File = "<unknown>";
      unsigned Line = 0;
      unsigned Column = 0;
      if (DILocation *Loc = I.getDebugLoc()) {
        File = Loc->getFilename();
        Line = Loc->getLine();
        Column = Loc->getColumn();
      }

      StringRef Func = F.getName();
      Value *FileStr = B.CreateGlobalStringPtr(File);
      Value *FuncStr = B.CreateGlobalStringPtr(Func);
      Value *LineV = ConstantInt::get(I32Ty, Line);
      Value *ColumnV = ConstantInt::get(I32Ty, Column);
      return std::array<Value *, 4>{FileStr, FuncStr, LineV, ColumnV};
    };

    auto checkInsertPoint = [&](Instruction &I, Value *ShadowValue) {
      if (auto *ShadowInst = dyn_cast<Instruction>(ShadowValue))
        if (ShadowInst->getParent() == I.getParent() && I.comesBefore(ShadowInst))
          return ShadowInst->getNextNode();
      return I.getNextNode();
    };

    auto checkFloat = [&](Instruction &I, Value *Actual, Value *ShadowValue,
                          int Op) {
      Instruction *InsertPoint = checkInsertPoint(I, ShadowValue);
      if (!InsertPoint)
        return;
      IRBuilder<> B(InsertPoint);
      auto Dbg = debugValues(B, I);
      if (Actual->getType()->isFloatTy()) {
        B.CreateCall(RT.Check,
                     {Actual, ShadowValue, ConstantInt::get(I32Ty, Op), Dbg[0],
                      Dbg[1], Dbg[2], Dbg[3]});
        return;
      }
      auto *FVT = floatVectorType(Actual->getType());
      if (!FVT)
        return;
      for (unsigned Lane = 0; Lane < FVT->getNumElements(); ++Lane) {
        Value *LaneV = ConstantInt::get(I32Ty, Lane);
        Value *ActualLane = B.CreateExtractElement(Actual, LaneV);
        Value *ShadowLane = B.CreateExtractElement(ShadowValue, LaneV);
        B.CreateCall(RT.Check,
                     {ActualLane, ShadowLane, ConstantInt::get(I32Ty, Op),
                      Dbg[0], Dbg[1], Dbg[2], Dbg[3]});
      }
    };

    auto checkBinary = [&](Instruction &I, Value *Actual, Value *ShadowValue,
                           Value *LHSShadow, Value *RHSShadow, int Op) {
      Instruction *InsertPoint = checkInsertPoint(I, ShadowValue);
      if (!InsertPoint)
        return;
      IRBuilder<> B(InsertPoint);
      auto Dbg = debugValues(B, I);
      if (Actual->getType()->isFloatTy()) {
        B.CreateCall(RT.CheckBinary,
                     {Actual, ShadowValue, LHSShadow, RHSShadow,
                      ConstantInt::get(I32Ty, Op), Dbg[0], Dbg[1], Dbg[2],
                      Dbg[3]});
        return;
      }
      auto *FVT = floatVectorType(Actual->getType());
      if (!FVT)
        return;
      for (unsigned Lane = 0; Lane < FVT->getNumElements(); ++Lane) {
        Value *LaneV = ConstantInt::get(I32Ty, Lane);
        Value *ActualLane = B.CreateExtractElement(Actual, LaneV);
        Value *ShadowLane = B.CreateExtractElement(ShadowValue, LaneV);
        Value *LHSLane = B.CreateExtractElement(LHSShadow, LaneV);
        Value *RHSLane = B.CreateExtractElement(RHSShadow, LaneV);
        B.CreateCall(RT.CheckBinary,
                     {ActualLane, ShadowLane, LHSLane, RHSLane,
                      ConstantInt::get(I32Ty, Op), Dbg[0], Dbg[1], Dbg[2],
                      Dbg[3]});
      }
    };

    if (!F.arg_empty()) {
      Instruction *Entry = &*F.getEntryBlock().getFirstInsertionPt();
      IRBuilder<> B(Entry);
      unsigned FloatIndex = 0;
      for (Argument &Arg : F.args()) {
        if (!Arg.getType()->isFloatTy())
          continue;
        Value *ArgShadow = B.CreateCall(
            RT.GetArgShadow,
            {ConstantInt::get(I32Ty, FloatIndex), &Arg},
            Arg.hasName() ? Arg.getName() + ".nsan.arg" : "nsan.arg");
        Shadow[&Arg] = ArgShadow;
        ++FloatIndex;
        Changed = true;
      }
    }

    SmallVector<PHINode *, 16> FloatPhis;
    for (BasicBlock &BB : F)
      for (PHINode &PN : BB.phis())
        if (isFloatValue(&PN))
          FloatPhis.push_back(&PN);

    for (PHINode *PN : FloatPhis) {
      IRBuilder<> B(PN);
      PHINode *ShadowPhi =
          B.CreatePHI(shadowType(PN->getType()), PN->getNumIncomingValues(),
                      PN->hasName() ? PN->getName() + ".nsan" : "nsan.phi");
      Shadow[PN] = ShadowPhi;
      Changed = true;
    }

    for (BasicBlock &BB : F) {
      for (PHINode &PN : BB.phis()) {
        auto *ShadowPhi = dyn_cast_or_null<PHINode>(Shadow.lookup(&PN));
        if (!ShadowPhi)
          continue;
        for (unsigned I = 0; I < PN.getNumIncomingValues(); ++I) {
          BasicBlock *Pred = PN.getIncomingBlock(I);
          Instruction *Term = Pred->getTerminator();
          Value *IncomingShadow = getShadow(PN.getIncomingValue(I), Term);
          ShadowPhi->addIncoming(IncomingShadow, Pred);
        }
      }
    }

    SmallVector<Instruction *, 64> Worklist;
    for (Instruction &I : instructions(F))
      Worklist.push_back(&I);

    for (Instruction *Inst : Worklist) {
      if (isa<PHINode>(Inst))
        continue;
      if (Inst->getFunction() &&
          startsWith(Inst->getFunction()->getName(), "__nsan_"))
        continue;

      if (auto *MCI = dyn_cast<MemCpyInst>(Inst)) {
        IRBuilder<> B(MCI->getNextNode() ? MCI->getNextNode() : MCI);
        B.CreateCall(RT.CopyShadowBytes,
                     {ptrAsI8(B, MCI->getDest()), ptrAsI8(B, MCI->getSource()),
                      sizeAsI64(B, MCI->getLength())});
        Changed = true;
        continue;
      }

      if (auto *MMI = dyn_cast<MemMoveInst>(Inst)) {
        IRBuilder<> B(MMI->getNextNode() ? MMI->getNextNode() : MMI);
        B.CreateCall(RT.MoveShadowBytes,
                     {ptrAsI8(B, MMI->getDest()), ptrAsI8(B, MMI->getSource()),
                      sizeAsI64(B, MMI->getLength())});
        Changed = true;
        continue;
      }

      if (auto *MSI = dyn_cast<MemSetInst>(Inst)) {
        IRBuilder<> B(MSI->getNextNode() ? MSI->getNextNode() : MSI);
        B.CreateCall(RT.ForgetShadowBytes,
                     {ptrAsI8(B, MSI->getDest()),
                      sizeAsI64(B, MSI->getLength())});
        Changed = true;
        continue;
      }

      if (auto *LI = dyn_cast<LoadInst>(Inst)) {
        if (!isFloatValue(LI))
          continue;
        Instruction *Next = LI->getNextNode();
        if (!Next)
          continue;
        IRBuilder<> B(Next);
        Value *ShadowValue = nullptr;
        if (LI->getType()->isFloatTy()) {
          ShadowValue = B.CreateCall(
              RT.ShadowLoad, {ptrAsI8(B, LI->getPointerOperand()), LI},
              LI->hasName() ? LI->getName() + ".nsan" : "nsan.load");
        } else {
          auto *FVT = floatVectorType(LI->getType());
          ShadowValue = UndefValue::get(shadowType(LI->getType()));
          for (unsigned Lane = 0; Lane < FVT->getNumElements(); ++Lane) {
            Value *LaneV = ConstantInt::get(I32Ty, Lane);
            Value *ActualLane = B.CreateExtractElement(LI, LaneV);
            Value *LanePtr =
                B.CreateGEP(FloatTy, LI->getPointerOperand(), LaneV);
            Value *LaneShadow =
                B.CreateCall(RT.ShadowLoad, {ptrAsI8(B, LanePtr), ActualLane},
                             "nsan.vload.lane");
            ShadowValue = B.CreateInsertElement(ShadowValue, LaneShadow, LaneV);
          }
        }
        Shadow[LI] = ShadowValue;
        checkFloat(*LI, LI, ShadowValue, NSAN_OP_LOAD);
        Changed = true;
        continue;
      }

      if (auto *SI = dyn_cast<StoreInst>(Inst)) {
        Value *Stored = SI->getValueOperand();
        IRBuilder<> B(SI);
        if (isFloatValue(Stored)) {
          Value *StoredShadow = getShadow(Stored, SI);
          if (Stored->getType()->isFloatTy()) {
            B.CreateCall(RT.ShadowStore,
                         {ptrAsI8(B, SI->getPointerOperand()), StoredShadow});
          } else {
            auto *FVT = floatVectorType(Stored->getType());
            for (unsigned Lane = 0; Lane < FVT->getNumElements(); ++Lane) {
              Value *LaneV = ConstantInt::get(I32Ty, Lane);
              Value *LaneShadow = B.CreateExtractElement(StoredShadow, LaneV);
              Value *LanePtr =
                  B.CreateGEP(FloatTy, SI->getPointerOperand(), LaneV);
              B.CreateCall(RT.ShadowStore, {ptrAsI8(B, LanePtr), LaneShadow});
            }
          }
          Changed = true;
          continue;
        }

        uint64_t StoreSize = typeStoreSize(Stored->getType());
        if (StoreSize > 0) {
          B.CreateCall(RT.ForgetShadowBytes,
                       {ptrAsI8(B, SI->getPointerOperand()),
                        ConstantInt::get(I64Ty, StoreSize)});
          Changed = true;
        }
        Changed = true;
        continue;
      }

      if (auto *BO = dyn_cast<BinaryOperator>(Inst)) {
        if (!isFloatValue(BO))
          continue;
        int Op = NSAN_OP_UNKNOWN;
        switch (BO->getOpcode()) {
        case Instruction::FAdd:
          Op = NSAN_OP_FADD;
          break;
        case Instruction::FSub:
          Op = NSAN_OP_FSUB;
          break;
        case Instruction::FMul:
          Op = NSAN_OP_FMUL;
          break;
        case Instruction::FDiv:
          Op = NSAN_OP_FDIV;
          break;
        case Instruction::FRem:
          Op = NSAN_OP_FREM;
          break;
        default:
          continue;
        }

        Instruction *Next = BO->getNextNode();
        if (!Next)
          continue;
        Value *LHSShadow = getShadow(BO->getOperand(0), Next);
        Value *RHSShadow = getShadow(BO->getOperand(1), Next);
        IRBuilder<> B(Next);
        Value *ShadowValue = nullptr;
        switch (BO->getOpcode()) {
        case Instruction::FAdd:
          ShadowValue = B.CreateFAdd(LHSShadow, RHSShadow, "nsan.fadd");
          break;
        case Instruction::FSub:
          ShadowValue = B.CreateFSub(LHSShadow, RHSShadow, "nsan.fsub");
          break;
        case Instruction::FMul:
          ShadowValue = B.CreateFMul(LHSShadow, RHSShadow, "nsan.fmul");
          break;
        case Instruction::FDiv:
          ShadowValue = B.CreateFDiv(LHSShadow, RHSShadow, "nsan.fdiv");
          break;
        case Instruction::FRem:
          ShadowValue = B.CreateFRem(LHSShadow, RHSShadow, "nsan.frem");
          break;
        default:
          break;
        }
        Shadow[BO] = ShadowValue;
        checkBinary(*BO, BO, ShadowValue, LHSShadow, RHSShadow, Op);
        Changed = true;
        continue;
      }

      if (auto *UO = dyn_cast<UnaryOperator>(Inst)) {
        if (UO->getOpcode() != Instruction::FNeg || !isFloatValue(UO))
          continue;
        Instruction *Next = UO->getNextNode();
        if (!Next)
          continue;
        Value *InputShadow = getShadow(UO->getOperand(0), Next);
        IRBuilder<> B(Next);
        Value *ShadowValue = B.CreateFNeg(InputShadow, "nsan.fneg");
        Shadow[UO] = ShadowValue;
        checkFloat(*UO, UO, ShadowValue, NSAN_OP_FNEG);
        Changed = true;
        continue;
      }

      if (auto *Sel = dyn_cast<SelectInst>(Inst)) {
        if (!isFloatValue(Sel))
          continue;
        Instruction *Next = Sel->getNextNode();
        if (!Next)
          continue;
        Value *TrueShadow = getShadow(Sel->getTrueValue(), Next);
        Value *FalseShadow = getShadow(Sel->getFalseValue(), Next);
        IRBuilder<> B(Next);
        Value *ShadowValue =
            B.CreateSelect(Sel->getCondition(), TrueShadow, FalseShadow,
                           Sel->hasName() ? Sel->getName() + ".nsan"
                                          : "nsan.select");
        Shadow[Sel] = ShadowValue;
        checkFloat(*Sel, Sel, ShadowValue, NSAN_OP_SELECT);
        Changed = true;
        continue;
      }

      if (auto *CI = dyn_cast<CastInst>(Inst)) {
        if (CI->getOpcode() != Instruction::FPTrunc || !isFloatValue(CI))
          continue;
        Instruction *Next = CI->getNextNode();
        if (!Next)
          continue;
        Value *Source = CI->getOperand(0);
        Value *ShadowValue = nullptr;
        if (Source->getType()->isDoubleTy() || doubleVectorType(Source->getType()))
          ShadowValue = Source;
        else if (isFloatValue(Source))
          ShadowValue = getShadow(Source, Next);
        else {
          IRBuilder<> B(Next);
          ShadowValue = B.CreateFPExt(CI, shadowType(CI->getType()), "nsan.fptrunc");
        }
        Shadow[CI] = ShadowValue;
        checkFloat(*CI, CI, ShadowValue, NSAN_OP_FPTRUNC);
        Changed = true;
        continue;
      }

      if (auto *Call = dyn_cast<CallInst>(Inst)) {
        if (Call->getCalledFunction() &&
            startsWith(Call->getCalledFunction()->getName(), "__nsan_"))
          continue;

        StringRef CalleeName = directCalleeName(*Call);
        if ((CalleeName == "memcpy" || CalleeName == "memmove") &&
            Call->arg_size() >= 3) {
          Instruction *InsertAt = Call->getNextNode() ? Call->getNextNode() : Call;
          IRBuilder<> B(InsertAt);
          FunctionCallee Copier =
              CalleeName == "memmove" ? RT.MoveShadowBytes : RT.CopyShadowBytes;
          B.CreateCall(Copier,
                       {ptrAsI8(B, Call->getArgOperand(0)),
                        ptrAsI8(B, Call->getArgOperand(1)),
                        sizeAsI64(B, Call->getArgOperand(2))});
          Changed = true;
          continue;
        }

        if (CalleeName == "memset" && Call->arg_size() >= 3) {
          Instruction *InsertAt = Call->getNextNode() ? Call->getNextNode() : Call;
          IRBuilder<> B(InsertAt);
          B.CreateCall(RT.ForgetShadowBytes,
                       {ptrAsI8(B, Call->getArgOperand(0)),
                        sizeAsI64(B, Call->getArgOperand(2))});
          Changed = true;
          continue;
        }

        unsigned FloatIndex = 0;
        for (Use &ArgUse : Call->args())
          if (ArgUse.get()->getType()->isFloatTy())
            ++FloatIndex;

        FunctionCallee MathModel =
            mathShadowCallee(directCalleeName(*Call), FloatIndex);
        if (isa<IntrinsicInst>(Call) && !MathModel)
          continue;

        unsigned ShadowArgIndex = 0;
        for (Use &ArgUse : Call->args()) {
          Value *Arg = ArgUse.get();
          if (!Arg->getType()->isFloatTy())
            continue;
          IRBuilder<> B(Call);
          Value *ArgShadow = getShadow(Arg, Call);
          B.CreateCall(RT.SetArgShadow,
                       {ConstantInt::get(I32Ty, ShadowArgIndex), ArgShadow});
          ++ShadowArgIndex;
          Changed = true;
        }

        Instruction *Next = Call->getNextNode();
        if (FloatIndex > 0 && Next) {
          IRBuilder<> B(Next);
          B.CreateCall(RT.ClearArgShadows,
                       {ConstantInt::get(I32Ty, FloatIndex)});
        }

        if (!Call->getType()->isFloatTy())
          continue;
        if (!Next)
          continue;
        IRBuilder<> B(Next);
        Value *ShadowValue = nullptr;
        if (MathModel) {
          SmallVector<Value *, 3> Args;
          Args.push_back(Call);
          for (Use &ArgUse : Call->args()) {
            Value *Arg = ArgUse.get();
            if (Arg->getType()->isFloatTy())
              Args.push_back(getShadow(Arg, Next));
          }
          ShadowValue = B.CreateCall(
              MathModel, Args,
              Call->hasName() ? Call->getName() + ".nsan.math" : "nsan.math");
        } else {
          ShadowValue = B.CreateCall(
              RT.GetReturnShadow, {Call},
              Call->hasName() ? Call->getName() + ".nsan.ret" : "nsan.ret");
        }
        Shadow[Call] = ShadowValue;
        checkFloat(*Call, Call, ShadowValue, NSAN_OP_CALL);
        Changed = true;
        continue;
      }

      if (auto *RI = dyn_cast<ReturnInst>(Inst)) {
        Value *Ret = RI->getReturnValue();
        if (!Ret || !Ret->getType()->isFloatTy())
          continue;
        IRBuilder<> B(RI);
        B.CreateCall(RT.SetReturnShadow, {getShadow(Ret, RI)});
        Changed = true;
      }
    }

    return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
  }
};

} // namespace

extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo
llvmGetPassPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "NumericalSanitizerPass", LLVM_VERSION_STRING,
          [](PassBuilder &PB) {
            PB.registerPipelineParsingCallback(
                [](StringRef Name, FunctionPassManager &FPM,
                   ArrayRef<PassBuilder::PipelineElement>) {
                  if (Name == "numerical-sanitizer") {
                    FPM.addPass(NumericalSanitizerPass());
                    return true;
                  }
                  return false;
                });
            PB.registerPipelineStartEPCallback(
                [](ModulePassManager &MPM, OptimizationLevel) {
                  FunctionPassManager FPM;
                  FPM.addPass(NumericalSanitizerPass());
                  MPM.addPass(createModuleToFunctionPassAdaptor(std::move(FPM)));
                });
          }};
}
