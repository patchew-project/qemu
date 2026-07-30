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

#include "TcgGenPass.hpp"
#include "CmdLineOptions.hpp"
#include "DebugInfo.hpp"
#include "Error.hpp"
#include "LinearizeBlocks.hpp"
#include "TcgEmit.hpp"
#include "TcgType.hpp"
#include "ValueMapping.hpp"

#include <llvm/ADT/PostOrderIterator.h>
#include <llvm/ADT/SmallBitVector.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/ADT/StringSet.h>
#include <llvm/Analysis/CallGraph.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/Debug.h>
#include <llvm/Support/FormatVariadic.h>
#include <llvm/Support/raw_ostream.h>

#define DEBUG_TYPE "tcg-gen-pass"

using namespace llvm;

struct TranslatedFunction {
    StringRef Name;
    std::string Decl;
    std::string Code;
    std::string DispatchCode;
    bool IsHelper;
    bool HasVectorTemporaries;
    bool NeedVectorSizeChangeOps;
};

static Expected<TranslatedFunction>
translateFunction(Module &M, FunctionAnalysisManager &FAM, Function *F,
                  const TcgGlobalMap &TcgGlobals,
                  const AnnotationMapTy &Annotations,
                  const DebugInfoMapTy &DebugInfo,
                  const SmallPtrSet<Function *, 16> HasTranslatedFunction) {
    assert(!F->isDeclaration());

    TranslatedFunction TF = {
        .Name = F->getName(),
    };

    {
        // Remove prefix for helper functions to get cleaner emitted names
        TF.IsHelper = TF.Name.consume_front("helper_");
    }

    std::string Body;
    raw_string_ostream Out(Body);
    CEmitter CE;
    TcgEmitter TE(Out, CE);
    TempAllocationData TAD;

    // Following block of function calls make up the majority of the backend,
    // the rest is mostly string emission.
    {
        // Linearize blocks, putting into a vector and breaking the LLVM IR by
        // some replacing branches with pseudo instructions.
        const LinearBlocks Blocks = linearizeBlocks(M, FAM, *F);
        LLVM_DEBUG({
            dbgs() << "Translating " << F->getName() << "\n";
            for (auto &BB : Blocks) {
                dbgs() << *BB << "\n";
            }
        });

        // Map arguments to `TcgV`, store results in `TempAllocationData`.
        if (auto Err = mapArguments(*F, Annotations, DebugInfo, TAD); Err) {
            return Err;
        }

        // Make a forward pass over the IR, and propagate constant expressions
        // forward, assigning them to values in `TempAllocationData`.
        if (auto Err = propagateConstantExpressions(CE, *F, Blocks, Annotations,
                                                    DebugInfo, TcgGlobals, TAD);
            Err) {
            return Err;
        }

        // Make a backward pass over the IR and assign temporary `TcgV`s to
        // non-constant expressions, attempt to reuse created temporaries.
        // Mapped temporaries are stored in `TempAllocationData`.
        if (auto Err = allocateTemporaries(*F, Blocks, Annotations, DebugInfo,
                                           CE, TcgGlobals, TAD);
            Err) {
            return Err;
        }

        // At this point all `Value`s in the function have been mapped to either
        // a temporary or a constant expression, with the exception of labels.
        // Make a forward pass over the IR and emit the final TCG operations.
        if (auto Err = mapTcgOperations(Blocks, TcgGlobals, Annotations,
                                        HasTranslatedFunction, TAD, TE, CE);
            Err) {
            return Err;
        }
    }

    Out.flush();

    raw_string_ostream OutFunc(TF.Code);
    raw_string_ostream HeaderWriter(TF.Decl);
    raw_string_ostream DispatchWriter(TF.DispatchCode);
    std::string DispatchCall;
    raw_string_ostream DispatchCallWriter(DispatchCall);
    bool IsVectorInst = false;

    if (StaticOutput) {
        HeaderWriter << "static ";
    }
    HeaderWriter << "void " << "emit_" << TF.Name << '(';
    SmallVector<TcgV, 4> CArgs;

    if (!F->getReturnType()->isVoidTy()) {
        assert(TAD.hasReturnValue());
        IsVectorInst = (TAD.ReturnValue.Kind == IrPtrToOffset);
        CArgs.push_back(TAD.ReturnValue);
    }

    for (const Argument &Arg : F->args()) {
        TcgV T = TAD.Map[&Arg];
        IsVectorInst |= (T.Kind == IrPtrToOffset);
        CArgs.push_back(T);
    }

    if (ForwardContext) {
        HeaderWriter << "DisasContext *ctx";
        if (!CArgs.empty()) {
            HeaderWriter << ", ";
        }
    }

    auto CArgIt = CArgs.begin();
    if (CArgIt != CArgs.end()) {
        HeaderWriter << TE.getType(*CArgIt) << ' ' << getName(*CArgIt);
        ++CArgIt;
    }
    while (CArgIt != CArgs.end()) {
        HeaderWriter << ", " << TE.getType(*CArgIt) << ' ' << getName(*CArgIt);
        ++CArgIt;
    }

    if (!IsVectorInst) {
        DispatchCallWriter << "emit_" << TF.Name << "(";
        auto CArgIt = CArgs.begin();
        if (CArgIt != CArgs.end()) {
            DispatchWriter << "static inline void gen_helper_" << TF.Name
                           << "(";
            for (int i = 0; i < CArgs.size(); ++i) {
                if (i > 0) {
                    DispatchWriter << ", ";
                }
                DispatchWriter << TE.getType(TE.materialize(CArgs[i])) << " "
                               << getName(CArgs[i]);
            }
            DispatchWriter << ")\n{\n";

            DispatchWriter << "emit_" << TF.Name << "(";
            for (int i = 0; i < CArgs.size(); ++i) {
                if (i > 0) {
                    DispatchWriter << ", ";
                }
                if (CArgs[i].Kind == IrImmediate) {
                    DispatchWriter << "tcgv_i" << CArgs[i].tcgBitWidth()
                                   << "_temp(" << getName(CArgs[i]) << ")->val";
                } else {
                    DispatchWriter << getName(CArgs[i]);
                }
            }
            DispatchWriter << ");\n}\n";
        }
    }

    // Copy over function declaration from header to source file
    HeaderWriter << ')';

    OutFunc << "// " << *F->getReturnType() << ' ' << F->getName() << '\n';
    OutFunc << HeaderWriter.str();
    OutFunc << " {\n";
    OutFunc << Body;
    OutFunc << "}\n";

    HeaderWriter << ';';

    if (MaxVectorTempBytes > 0 and
        TE.allocatedVectorMemory() > MaxVectorTempBytes) {
        return mkError(formatv("Uses too much vector memory: {0} > {1}",
                               TE.allocatedVectorMemory(), MaxVectorTempBytes)
                           .str());
    }

    if (MaxVectorInstructions > 0 and
        TE.numVectorInstructions() > MaxVectorInstructions) {
        return mkError(formatv("Uses too many vector instructions: {0} > {1}",
                               TE.numVectorInstructions(),
                               MaxVectorInstructions)
                           .str());
    }

    TF.HasVectorTemporaries = (TE.allocatedVectorMemory() > 0);
    TF.NeedVectorSizeChangeOps = TE.NeedVectorSizeChangeOps;

    HeaderWriter.flush();
    DispatchWriter.flush();
    DispatchCallWriter.flush();

    return TF;
}

PreservedAnalyses TcgGenPass::run(Module &M, ModuleAnalysisManager &MAM) {
    auto &CG = MAM.getResult<CallGraphAnalysis>(M);
    auto &FAM =
        MAM.getResult<FunctionAnalysisManagerModuleProxy>(M).getManager();

    // Vector of translation results, order matters, lower indices are emitted
    // higher up in the output file, and may be used by the higher indices.
    SmallVector<TranslatedFunction, 16> TranslatedFunctions;
    // Two sets used for quickly looking up whether or not a function has
    // already been translated, or the translation failed.
    SmallPtrSet<Function *, 16> FailedToTranslateFunction;
    SmallPtrSet<Function *, 16> HasTranslatedFunction;
    bool NeedVectorMem = false;
    bool NeedVectorSizeChangeOps = false;
    for (Function &F : M) {
        if (F.isDeclaration()) {
            continue;
        }

        // Depth first traversal of call graph.  Needed to ensure called
        // functions are translated before the current function.
        CallGraphNode *Node = CG[&F];
        for (auto *N : make_range(po_begin(Node), po_end(Node))) {
            Function *F = N->getFunction();

            // If F in the call graph has already been translated and failed,
            // abort translation of the current function.
            if (FailedToTranslateFunction.contains(F)) {
                break;
            }

            // Skip translation of invalid functions or functions that have
            // already been translated.
            if (!F or F->isDeclaration() or HasTranslatedFunction.contains(F)) {
                continue;
            }

            // Reset variable IDs for the current function, since al TcgVs are
            // function-local.
            TcgV::resetId();

            auto Translated =
                translateFunction(M, FAM, F, TcgGlobals, Annotations, DebugInfo,
                                  HasTranslatedFunction);
            if (!Translated) {
                FailedToTranslateFunction.insert(F);
                LLVM_DEBUG({
                    dbgs() << F->getName() << ": " << Translated.takeError()
                           << "\n";
                });
                if (ErrorOnTranslationFailure) {
                    return PreservedAnalyses::all();
                }
            } else {
                TranslatedFunctions.push_back(*Translated);
                HasTranslatedFunction.insert(F);
                NeedVectorMem |= Translated->HasVectorTemporaries;
                NeedVectorSizeChangeOps |= Translated->NeedVectorSizeChangeOps;
            }
        }
    }

    // Preamble
    OutSource << "#include \"qemu/osdep.h\"\n";
    OutSource << "#include \"qemu/log.h\"\n";
    OutSource << "#include \"cpu.h\"\n";
    OutSource << "#include \"translate.h\"\n";
    OutSource << "#include \"tcg/tcg-op.h\"\n";
    OutSource << "#include \"tcg/tcg-op-gvec.h\"\n";
    OutSource << "#include \"tcg/tcg.h\"\n";
    // OutSource << "#include \"exec/helper-gen.h\"\n";
    if (ForwardContext) {
        OutSource << "#include \"translate.h\"\n";
    }
    if (!StaticOutput) {
        OutSource << "#include \"tcg/tcg-global-mappings.h\"\n";
    }
    if (MmuIndexFunction.size() > 0) {
        OutSource << "#include \"exec/translation-block.h\"";
    }
    OutSource << '\n';

    if (!StaticOutput) {
        OutSource << "#include \""
                  << HeaderPath.substr(HeaderPath.find_last_of('/') + 1)
                  << "\"\n";
        OutSource << '\n';

        // Emit extern definitions for all global TCGv_* that are mapped
        // to the CPUState.
        for (size_t TypeIndex = 0; TypeIndex < TcgGlobals.size(); ++TypeIndex) {
            for (auto &P : TcgGlobals[TypeIndex]) {
                const TcgGlobal &Global = P.second;
                auto Size = ValueSize::fromBitWidth(Global.Size);
                assert(Size);
                OutSource << "extern " << "TCGv_i" << (int)Size->TcgBitWidth
                          << " " << Global.Code;
                if (Global.NumElements > 1) {
                    OutSource << "[" << Global.NumElements << "]";
                }
                OutSource << ";\n";
            }
        }
    }

    if (NeedVectorSizeChangeOps) {
        emitVectorSizeChangeOps(OutSource, OutHelpers, VL);
    }

    if (NeedVectorMem) {
        emitVectorMem(OutSource);
    }

    if (ForwardContext) {
        OutHeader << "struct DisasContext;\n";
        OutHeader << "typedef struct DisasContext DisasContext;\n";
        OutSource << "#include \"translate.h\"\n";
    }

    // Emit translated functions
    for (auto &TF : TranslatedFunctions) {
        OutSource << TF.Code << '\n';
        OutHeader << TF.Decl << '\n';
        OutEnabled << TF.Name << '\n';
    }

    // Emit a dispatched to go from helper function address to our
    // emitted code, if we succeeded.
    OutHeader << "bool helper_to_tcg_dispatcher(void *func, TCGTemp *ret_temp, "
                 "int nargs, TCGTemp **args);\n";

    OutHeader << "#include \"tcg/helper-info.h\"\n";
    OutHeader << "#include \"exec/helper-head.h.inc\"\n";
    for (Function &F : M) {
        StringRef Name = F.getName();
        if (!Name.consume_front("helper_")) {
            continue;
        }
        if (!HasTranslatedFunction.contains(&F)) {
            SmallVector<Type *> ArgTys;
            for (auto &A : F.args()) {
                ArgTys.push_back(A.getType());
            }
            emitHelperGen(OutHeader, Name, F.getReturnType(), ArgTys);
        }
    }
    for (TranslatedFunction &TF : TranslatedFunctions) {
        OutHeader << TF.DispatchCode;
    }

    return PreservedAnalyses::all();
}
