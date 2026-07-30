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

#include "IdentityMap.hpp"
#include "PseudoInst.hpp"
#include "TcgType.hpp"

#include <llvm/ADT/SmallVector.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/InstIterator.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Value.h>

using namespace llvm;

void identityMap(Module &M, Function &F) {
    SmallVector<Instruction *, 8> InstToErase;

    for (Instruction &I : instructions(F)) {
        auto *ZExt = dyn_cast<ZExtInst>(&I);
        if (ZExt) {
            auto *SrcIntTy =
                dyn_cast<IntegerType>(ZExt->getOperand(0)->getType());
            auto *DstIntTy = dyn_cast<IntegerType>(ZExt->getType());
            if (!SrcIntTy or !DstIntTy) {
                continue;
            }

            auto SrcSize = ValueSize::fromLlvmType(SrcIntTy);
            auto DstSize = ValueSize::fromLlvmType(DstIntTy);
            if (!SrcSize or !DstSize) {
                continue;
            }

            // TODO: Hack again to get bit width from icmp arguments, should
            // widen in canonicalization phase.
            if (SrcSize->LlvmBitWidth == 1) {
                auto *ICmp = dyn_cast<ICmpInst>(ZExt->getOperand(0));
                if (ICmp) {
                    auto *ICmpOp = ICmp->getOperand(0);
                    auto OpSize = ValueSize::fromLlvmType(
                        cast<IntegerType>(ICmpOp->getType()));
                    if (!OpSize) {
                        continue;
                    }
                    SrcSize = *OpSize;
                }
            }

            // Only identity map when TCG sizes match.
            if (SrcSize->TcgBitWidth != DstSize->TcgBitWidth) {
                continue;
            }

            IRBuilder<> Builder(&I);
            ZExt->replaceAllUsesWith(createPseudoInstCall(
                M, Builder, IdentityMap, DstIntTy, {ZExt->getOperand(0)}));
            InstToErase.push_back(&I);
        } else if (auto *Load = dyn_cast<LoadInst>(&I);
                   Load and Load->getType()->isVectorTy()) {
            Value *Ptr = Load->getPointerOperand();
            IRBuilder<> Builder(&I);
            Load->replaceAllUsesWith(createPseudoInstCall(
                M, Builder, IdentityMap, Load->getType(), {Ptr}));
            InstToErase.push_back(&I);
        } else if (isa<FreezeInst>(&I)) {
            IRBuilder<> Builder(&I);
            I.replaceAllUsesWith(createPseudoInstCall(
                M, Builder, IdentityMap, I.getType(), {I.getOperand(0)}));
            InstToErase.push_back(&I);
        }
    }

    for (Instruction *I : InstToErase) {
        I->eraseFromParent();
    }
}
