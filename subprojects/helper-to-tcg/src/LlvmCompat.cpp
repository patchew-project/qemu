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

#include "LlvmCompat.hpp"

#include <llvm/CodeGen/CommandFlags.h>
#include <llvm/Support/CodeGen.h>

#include <string>

// Static variables required by LLVM
//
// Defining RegisterCodeGenFlags with static duration registers extra
// codegen commandline flags for specifying the target arch.
static llvm::codegen::RegisterCodeGenFlags CGF;
static llvm::ExitOnError ExitOnErr;

namespace compat {

using namespace llvm;

size_t getTypeAllocSize(const llvm::DataLayout &DL, llvm::Type *Ty) {
#if LLVM_VERSION_MAJOR >= 16
    return DL.getTypeAllocSize(Ty).getFixedValue();
#else
    return DL.getTypeAllocSize(Ty).getFixedSize();
#endif
}

bool isFunctionQemuHelper(StringRef Name) {
#if LLVM_VERSION_MAJOR >= 18
    return Name.starts_with("helper_");
#else
    return Name.startswith("helper_");
#endif
}

bool isFunctionQemuLoadStore(StringRef Name) {
#if LLVM_VERSION_MAJOR >= 18
    return Name.starts_with("cpu_ld") or Name.starts_with("cpu_st");
#else
    return Name.startswith("cpu_ld") or Name.startswith("cpu_st");
#endif
}

llvm::TargetMachine *getTargetMachine(llvm::Triple &TheTriple) {
    const TargetOptions Options{};
    std::string Error;
    const Target *TheTarget = llvm::TargetRegistry::lookupTarget(
        llvm::codegen::getMArch(), TheTriple, Error);
    // Some modules don't specify a triple, and this is okay.
    if (!TheTarget) {
        return nullptr;
    }

#if LLVM_VERSION_MAJOR >= 18
    auto Level = llvm::CodeGenOptLevel::Aggressive;
#else
    auto Level = llvm::CodeGenOpt::Aggressive;
#endif

#if LLVM_VERSION_MAJOR >= 21
    return TheTarget->createTargetMachine(
        llvm::Triple(TheTriple.getTriple()), llvm::codegen::getCPUStr(),
        llvm::codegen::getFeaturesStr(), Options,
        llvm::codegen::getExplicitRelocModel(),
        llvm::codegen::getExplicitCodeModel(), Level);
#else
    return TheTarget->createTargetMachine(
        TheTriple.getTriple(), llvm::codegen::getCPUStr(),
        llvm::codegen::getFeaturesStr(), Options, {}, {}, Level);
#endif
}

} // namespace compat
