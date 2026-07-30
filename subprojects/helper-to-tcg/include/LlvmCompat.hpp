//
//  Copyright(c) 2026 rev.ng Labs Srl. All Rights Reserved.
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation; either version 2 of the License, or
//  (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program; if not, see <http://www.gnu.org/licenses/>.
//

#pragma once

//
// The purpose of this file is to both collect and hide most api-specific
// changes of LLVM [10,14]. Hopefully making it easier to keep track of the
// changes necessary to support our targeted versions.
//
// Note some #ifdefs still remain throughout the codebase for larger codeblocks
// that are specific enough such that pulling them here would be more cumbersome
// than it's worth.
//

#include <llvm/Analysis/TargetTransformInfo.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/PassManager.h>
#include <llvm/IR/PatternMatch.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Passes/OptimizationLevel.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Transforms/Utils/UnifyFunctionExitNodes.h>

#include <stdint.h>

namespace compat {

constexpr auto OpenFlags = llvm::sys::fs::OF_TextWithCRLF;

using OptimizationLevel = llvm::OptimizationLevel;

constexpr auto LTOPhase = llvm::ThinOrFullLTOPhase::None;

inline llvm::PassBuilder createPassBuilder(llvm::TargetMachine *TM,
                                           llvm::PipelineTuningOptions &PTO) {
#if LLVM_VERSION_MAJOR >= 16
    return llvm::PassBuilder(TM, PTO, std::nullopt);
#else
    return llvm::PassBuilder(TM, PTO, llvm::None);
#endif
}

// LLVM 16 changes the getFixedSize() -> getFixedValue()
size_t getTypeAllocSize(const llvm::DataLayout &DL, llvm::Type *Ty);

// These rely on string comparison with QEMU names using StringRef::startswith,
// which changed to StringRef::starts_with in LLVM 18.
bool isFunctionQemuHelper(llvm::StringRef Name);
bool isFunctionQemuLoadStore(llvm::StringRef Name);

// LLVM 21 moved to initializing a TargetTransformInfo via unique_ptr,
// instead of by value.
template <typename T>
auto makeTTI(llvm::TargetMachine *TM, const llvm::Function &F) {
    // Use explicit TargetTransformInfo() contructor
#if LLVM_VERSION_MAJOR >= 21
    return llvm::TargetTransformInfo(std::make_unique<T>(TM, F));
#else
    return llvm::TargetTransformInfo(T(TM, F));
#endif
}

// Note, since constexpr auto is used to alias a function, default
// arguments won't work.
//
// LLVM 20 deprecated getDeclaration() in favour of getOrInsertDeclaration()
namespace Intrinsic {
#if LLVM_VERSION_MAJOR >= 20
constexpr auto getOrInsertDeclaration = llvm::Intrinsic::getOrInsertDeclaration;
#else
constexpr auto getOrInsertDeclaration = llvm::Intrinsic::getDeclaration;
#endif
} // namespace Intrinsic

// Wrapper to convert Function- to Module analysis manager
template <typename T>
inline const typename T::Result *
getModuleAnalysisManagerProxyResult(llvm::FunctionAnalysisManager &FAM,
                                    llvm::Function &F) {
    auto &MAMProxy = FAM.getResult<llvm::ModuleAnalysisManagerFunctionProxy>(F);
    return MAMProxy.getCachedResult<T>(*F.getParent());
}

llvm::TargetMachine *getTargetMachine(llvm::Triple &TheTriple);

//
// LLVM 11 and below does not define the UnifyFunctionExitNodes pass
// for the new pass manager.  Copy over the definition and use it for
// 11 and below.
//
using llvm::UnifyFunctionExitNodesPass;

inline uint32_t getVectorElementCount(const llvm::VectorType *VecTy) {
    auto ElementCount = VecTy->getElementCount();
    return ElementCount.getFixedValue();
}

//
// PatternMatch
//

#define compat_m_InsertElt llvm::PatternMatch::m_InsertElt
#define compat_m_Shuffle llvm::PatternMatch::m_Shuffle
#define compat_m_ZeroMask llvm::PatternMatch::m_ZeroMask

} // namespace compat
