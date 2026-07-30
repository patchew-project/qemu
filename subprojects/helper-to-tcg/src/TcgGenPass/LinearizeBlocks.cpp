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

#include "LinearizeBlocks.hpp"
#include "PseudoInst.hpp"

#include <llvm/ADT/PostOrderIterator.h>
#include <llvm/ADT/SmallPtrSet.h>
#include <llvm/ADT/SmallSet.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/Analysis/RegionInfo.h>
#include <llvm/Analysis/VectorUtils.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Dominators.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/InstIterator.h>
#include <llvm/IR/InstrTypes.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/PassManager.h>
#include <llvm/IR/PatternMatch.h>
#include <llvm/Support/Casting.h>

using namespace llvm;
using namespace PatternMatch;

// Needed to track and remove instructions not handled by a subsequent dead code
// elimination, this applies to calls to pseudo instructions in particular.
using EraseInstVec = SmallVector<Instruction *, 16>;

static void convertICmpBrToPseudInst(LLVMContext &Context, RegionInfo &RI,
                                     EraseInstVec &InstToErase, Module &M,
                                     Instruction *I, BasicBlock *NextBB) {
    auto *ICmp = dyn_cast<ICmpInst>(I);
    if (!ICmp) {
        return;
    }

    // Since we want to remove the icmp instruction we ensure that
    // all uses are branch instructions that can be converted into
    // @brcond.* calls.
    for (User *U : ICmp->users()) {
        if (!isa<BranchInst>(U)) {
            return;
        }
    }

    Value *Op0 = ICmp->getOperand(0);
    Value *Op1 = ICmp->getOperand(1);
    auto *CmpIntTy = dyn_cast<IntegerType>(Op0->getType());
    if (!CmpIntTy) {
        return;
    }
    for (User *U : ICmp->users()) {
        auto *Br = cast<BranchInst>(U);

        BasicBlock *True = Br->getSuccessor(0);
        BasicBlock *False = Br->getSuccessor(1);

        IRBuilder<> Builder(Br);

        // TODO: This is strange, indeally the unreachable branch should have
        // been optimized out, here we invert the conditional branch if we can
        // fallthrough to the reachable branch.
        bool TrueUnreachable =
            True->getTerminator()->getOpcode() == Instruction::Unreachable and
            False->getTerminator()->getOpcode() != Instruction::Unreachable;

        // If the next basic block is either of our true/false
        // branches, we can fallthrough instead of branching.
        bool Fallthrough = (NextBB == True or NextBB == False);

        // If the succeeding basic block is the true branch we
        // invert the condition so we can fallthrough instead.
        ICmpInst::Predicate Predicate;
        if (NextBB == True or (TrueUnreachable and NextBB == False)) {
            std::swap(True, False);
            Predicate = ICmp->getInversePredicate();
        } else {
            Predicate = ICmp->getPredicate();
        }

        createPseudoInstCall(M, Builder, Brcond, Builder.getVoidTy(),
                             {Builder.getInt8(Fallthrough),
                              ConstantInt::get(CmpIntTy, Predicate), Op0, Op1,
                              True, False});

        InstToErase.push_back(Br);
    }
    InstToErase.push_back(ICmp);
}

LinearBlocks linearizeBlocks(Module &M, FunctionAnalysisManager &FAM,
                             Function &F) {
    assert(!F.isDeclaration());

    LinearBlocks Blocks{};

    LLVMContext &Context = F.getContext();
    EraseInstVec InstToErase;
    auto &RI = FAM.getResult<RegionInfoAnalysis>(F);

    ReversePostOrderTraversal<Function *> RPOT(&F);
    for (auto BBIt = RPOT.begin(); BBIt != RPOT.end(); ++BBIt) {
        Blocks.push_back(*BBIt);
    }

    for (int i = 0; i < Blocks.size(); ++i) {
        BasicBlock *BB = Blocks[i];
        BasicBlock *NextBB = (i + 1 < Blocks.size()) ? Blocks[i + 1] : nullptr;
        for (Instruction &I : *BB) {
            convertICmpBrToPseudInst(Context, RI, InstToErase, M, &I, NextBB);
        }
    }

    // Finally clean up instructions we need to remove manually
    for (Instruction *I : InstToErase) {
        I->eraseFromParent();
    }

    return Blocks;
}
