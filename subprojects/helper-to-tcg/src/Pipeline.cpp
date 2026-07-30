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

#include "CmdLineOptions.hpp"
#include "LlvmCompat.hpp"
#include "PrepareForOptPass.hpp"

#if LLVM_VERSION_MAJOR == 15
#include <llvm/ADT/Triple.h>
#else
#include <llvm/TargetParser/Triple.h>
#endif
#include <llvm/Analysis/AliasAnalysis.h>
#include <llvm/Analysis/CGSCCPassManager.h>
#include <llvm/Analysis/LoopAnalysisManager.h>
#include <llvm/Analysis/TargetTransformInfo.h>
#include <llvm/CodeGen/BasicTTIImpl.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/PassManager.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/InitializePasses.h>
#include <llvm/Linker/Linker.h>
#include <llvm/PassRegistry.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/Debug.h>
#include <llvm/Support/InitLLVM.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Transforms/Scalar/SROA.h>

#define DEBUG_TYPE "pipeline"

using namespace llvm;

cl::OptionCategory Cat("helper-to-tcg Options");

// Options for pipeline
cl::opt<std::string> InputFile(cl::Positional, cl::desc("[input LLVM module]"),
                               cl::cat(Cat));

// Debug options
#ifndef NDEBUG
static cl::opt<bool> Debug("debug", cl::desc("Enable debug logging (slow)"),
                           cl::init(false), cl::cat(Cat));
static cl::opt<std::string>
    DebugOnly("debug-only",
              cl::desc("Enable debug logging for a specific pass"),
              cl::init(""), cl::cat(Cat));
#endif

// Options for PrepareForOptPass
cl::opt<bool> TranslateAllHelpers(
    "translate-all-helpers", cl::init(false),
    cl::desc("Translate all functions starting with helper_*"), cl::cat(Cat));

// Define a TargetTransformInfo (TTI) subclass, this allows for overriding
// common per-llvm-target information expected by other LLVM passes, such
// as the width of the largest scalar/vector registers.  Needed for consistent
// behaviour across different hosts.
class TcgTTI : public BasicTTIImplBase<TcgTTI> {
    friend class BasicTTIImplBase<TcgTTI>;

    // We need to provide ST, TLI, getST(), getTLI()
    const TargetSubtargetInfo *ST;
    const TargetLoweringBase *TLI;

    const TargetSubtargetInfo *getST() const { return ST; }
    const TargetLoweringBase *getTLI() const { return TLI; }

  public:
    // Initialize ST and TLI from the target machine, e.g. if we're
    // targeting x86 we'll get the Subtarget and TargetLowering to
    // match that architechture.
    TcgTTI(TargetMachine *TM, Function const &F)
        : BasicTTIImplBase(TM, F.getParent()->getDataLayout()),
          ST(TM->getSubtargetImpl(F)), TLI(ST->getTargetLowering()) {}

    TypeSize getRegisterBitWidth(TargetTransformInfo::RegisterKind K) const {
        switch (K) {
        case TargetTransformInfo::RGK_Scalar:
            // We pretend we always support 64-bit registers
            return TypeSize::getFixed(64);
        case TargetTransformInfo::RGK_FixedWidthVector:
            // We pretend we always support 2048-bit vector registers
            return TypeSize::getFixed(2048);
        case TargetTransformInfo::RGK_ScalableVector:
            return TypeSize::getScalable(0);
        default:
            abort();
        }
    }
};

int main(int argc, char **argv) {
    InitLLVM X(argc, argv);
    cl::HideUnrelatedOptions(Cat);

    InitializeAllTargets();
    InitializeAllTargetMCs();
    PassRegistry &Registry = *PassRegistry::getPassRegistry();
    initializeCore(Registry);
    initializeScalarOpts(Registry);
    initializeVectorization(Registry);
    initializeAnalysis(Registry);
    initializeTransformUtils(Registry);
    initializeInstCombine(Registry);
    initializeTarget(Registry);

    cl::ParseCommandLineOptions(argc, argv);

    // Enable debug logging, -debug and -debug-only are normally provided by
    // LLVMs default debug options, but since we want to hide most default
    // options and keep them visible during a debug build it's easiest to add
    // them back manually
#ifndef NDEBUG
    DebugFlag = Debug;
    if (!DebugOnly.empty()) {
        setCurrentDebugType(DebugOnly.c_str());
    }
#endif

    LLVMContext Context;
    SMDiagnostic Err;
    std::unique_ptr<Module> M = parseIRFile(InputFile, Err, Context);

    // Create a new TargetMachine to represent a TCG target,
    // we use x86_64 as a base and derive from that using a
    // TargetTransformInfo to provide allowed scalar and vector
    // register sizes.
    Triple ModuleTriple("x86_64-pc-unknown");
    assert(ModuleTriple.getArch());
    TargetMachine *TM = compat::getTargetMachine(ModuleTriple);

    PipelineTuningOptions PTO;
    PassBuilder PB = compat::createPassBuilder(TM, PTO);
    LoopAnalysisManager LAM;
    FunctionAnalysisManager FAM;
    CGSCCAnalysisManager CGAM;
    ModuleAnalysisManager MAM;

    // Register our TargetIrAnalysis pass using our own TTI
    FAM.registerPass([&] {
        return TargetIRAnalysis(
            [&TM](const Function &F) -> TargetIRAnalysis::Result {
                return compat::makeTTI<TcgTTI>(TM, F);
            });
    });

    // Register other default LLVM Analyses
    PB.registerFunctionAnalyses(FAM);
    PB.registerModuleAnalyses(MAM);
    PB.registerLoopAnalyses(LAM);
    PB.registerCGSCCAnalyses(CGAM);
    PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);

    ModulePassManager MPM;

    //
    // Start by Filtering out functions we don't want to translate,
    // following by a pass that removes `noinline`s that are inserted
    // by clang on -O0. We finally run a UnifyExitNodesPass to make sure
    // the helpers we parse only has a single exit.
    //

    {
        FunctionPassManager FPM;
#if LLVM_VERSION_MAJOR >= 16
        FPM.addPass(SROAPass(SROAOptions::ModifyCFG));
#else
        FPM.addPass(SROAPass());
#endif
        MPM.addPass(createModuleToFunctionPassAdaptor(std::move(FPM)));
    }

    // TODO: Get pass results via dependencies instead? Adds more boiler-plate
    // but is correlct in LLVM-terms.

    AnnotationMapTy Annotations;
    MPM.addPass(PrepareForOptPass(Annotations));

    {
        FunctionPassManager FPM;
        FPM.addPass(compat::UnifyFunctionExitNodesPass());
        MPM.addPass(createModuleToFunctionPassAdaptor(std::move(FPM)));
    }

    return 0;
}
