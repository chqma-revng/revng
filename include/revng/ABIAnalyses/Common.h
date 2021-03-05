#pragma once

//
// This file is distributed under the MIT License. See LICENSE.md for details.
//

#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/Casting.h"

#include "revng/BasicAnalyses/GeneratedCodeBasicInfo.h"
#include "revng/MFP/MFP.h"
#include "revng/Model/Binary.h"
#include "revng/StackAnalysis/StackAnalysis.h"
#include "revng/Support/revng.h"

namespace ABIAnalyses {

using llvm::Instruction;
using llvm::GlobalVariable;
using llvm::SmallSet;
using llvm::SmallVector;
using llvm::Value;
using llvm::LoadInst;
using llvm::StoreInst;

using Register = model::Register::Values;

enum TransferKind {
  Read,
  Write,
  WeakWrite,
  TheCall,
  None,
  // legacy transfer functions
  ReturnFromYes,
  ReturnFromMaybe,
  ReturnFromNoOrDead,
  ReturnFromUnknown,
  UnknownFunctionCall
};

struct ABIAnalysis {
private:
  SmallSet<GlobalVariable *, 20> ABIRegisters;
  SmallVector<GlobalVariable *, 20> RegisterList;
  const Instruction *CallSite;

public:
  ABIAnalysis(const GeneratedCodeBasicInfo &GCBI) :
    ABIAnalysis(nullptr, GCBI){};

  ABIAnalysis(const Instruction *CS, const GeneratedCodeBasicInfo &GCBI) :
    RegisterList(), CallSite(CS) {

    for (auto *CSV : GCBI.abiRegisters())
      if (CSV) {
        ABIRegisters.insert(CSV);
        RegisterList.emplace_back(CSV);
      }
  };

  const SmallVector<GlobalVariable *, 20> getRegisters() const {
    return RegisterList;
  }

  bool isABIRegister(const Value *) const;

  TransferKind classifyInstruction(const Instruction *) const;

  SmallVector<const GlobalVariable*, 1> getRegistersWritten(const Instruction *) const;

  SmallVector<const GlobalVariable*, 1> getRegistersRead(const Instruction *) const;
};

template<typename MFI, typename CoreLattice, typename KeyT>
typename CoreLattice::LatticeElement
getOrDefault(const typename MFI::LatticeElement &S, const KeyT &K) {
  auto V = S.find(K);
  if (V != S.end()) {
    return S.lookup(K);
  }
  return CoreLattice::DefaultLatticeElement;
}

template<typename MFI, typename CoreLattice>
typename MFI::LatticeElement
combineValues(const typename MFI::LatticeElement &Lh,
              const typename MFI::LatticeElement &Rh) {

  typename MFI::LatticeElement New = Lh;
  for (const auto &[Reg, S] : Rh) {
    New[Reg] = CoreLattice::combineValues(getOrDefault<MFI, CoreLattice>(New, Reg), getOrDefault<MFI, CoreLattice>(Rh, Reg));
  }
  return New;
}

template<typename MFI, typename CoreLattice>
bool isLessOrEqual(const typename MFI::LatticeElement &Lh,
                   const typename MFI::LatticeElement &Rh) {
  for (auto &[Reg, S] : Lh) {
    if (!CoreLattice::isLessOrEqual(getOrDefault<MFI, CoreLattice>(Lh, Reg), getOrDefault<MFI, CoreLattice>(Rh, Reg))) {
      return false;
    }
  }
  for (auto &[Reg, S] : Rh) {
    if (!CoreLattice::isLessOrEqual(getOrDefault<MFI, CoreLattice>(Lh, Reg), getOrDefault<MFI, CoreLattice>(Rh, Reg))) {
      return false;
    }
  }
  return true;
}

inline bool ABIAnalysis::isABIRegister(const Value *V) const {
  if (const auto &G = dyn_cast<GlobalVariable>(V)) {
    if (ABIRegisters.count(G) != 0) {
      return true;
    }
  }
  return false;
}

inline bool isCallSiteBlock(const llvm::BasicBlock* B) {
  if (auto *C = llvm::dyn_cast<llvm::CallInst>(&*B->getFirstInsertionPt())) {
    if (C->getCalledFunction()->getName() == "precall_hook") {
      return true;
    }
  }
  return false;
}

inline const llvm::Instruction * getPreCallHook(const llvm::BasicBlock* B) {
  if (isCallSiteBlock(B)) {
    return &*B->getFirstInsertionPt();
  }
  return nullptr;
}


inline const llvm::Instruction * getPostCallHook(const llvm::BasicBlock* B) {
  if (isCallSiteBlock(B)) {
    return B->getTerminator()->getPrevNode();
  }
  return nullptr;
}

inline TransferKind
ABIAnalysis::classifyInstruction(const Instruction *I) const {
  switch (I->getOpcode()) {
  case Instruction::Store: {
    auto S = cast<StoreInst>(I);
    if (isABIRegister(S->getPointerOperand())) {
      if (isCallSiteBlock(I->getParent())) {
        return WeakWrite;
      }
      return Write;
    }
    break;
  }
  case Instruction::Load: {
    auto L = cast<LoadInst>(I);
    if (isABIRegister(L->getPointerOperand())) {
      return Read;
    }
    break;
  }
  case Instruction::Call: {
    if (I == CallSite) {
      return TheCall;
    }
  }
  }
  return None;
}

inline SmallVector<const GlobalVariable*, 1>
ABIAnalysis::getRegistersWritten(const Instruction *I) const {
  SmallVector<const GlobalVariable*, 1> Result;
  switch (I->getOpcode()) {
  case Instruction::Store: {
    auto S = cast<StoreInst>(I);
    if (isABIRegister(S->getPointerOperand())) {
      Result.push_back(cast<GlobalVariable>(S->getPointerOperand()));
    }
    break;
  }
  }
  return Result;
}

inline SmallVector<const GlobalVariable*, 1>
ABIAnalysis::getRegistersRead(const Instruction *I) const {
  SmallVector<const GlobalVariable*, 1> Result;
  switch (I->getOpcode()) {
  case Instruction::Load: {
    auto L = cast<LoadInst>(I);
    if (isABIRegister(L->getPointerOperand())) {
      Result.push_back(cast<GlobalVariable>(L->getPointerOperand()));
    }
    break;
  }
  }
  return Result;
}
} // namespace ABIAnalyses
