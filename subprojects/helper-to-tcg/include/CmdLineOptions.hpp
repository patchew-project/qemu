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

#include <llvm/Support/CommandLine.h>

// Options for pipeline
extern llvm::cl::list<std::string> InputFiles;
// Options for PrepareForOptPass
extern llvm::cl::opt<bool> TranslateAllHelpers;
// Options for PrepareForTcgPass
extern llvm::cl::opt<std::string> TcgGlobalMappingsName;
extern llvm::cl::opt<std::string> UserPCRelBranchFunc;
extern llvm::cl::opt<std::string> UserPCRelBranchFallthroughFunc;
extern llvm::cl::opt<std::string> UserPCRelBranchConditionalFunc;
// Options for TcgEmit
extern llvm::cl::opt<std::string> MmuIndexFunction;
extern llvm::cl::opt<std::string> TempVectorBlock;
extern llvm::cl::opt<uint32_t> MaxVectorInstructions;
extern llvm::cl::opt<uint32_t> MaxVectorTempBytes;
// Options for MapTemporaries
extern llvm::cl::opt<uint32_t> GuestPtrSize;
// Options for TcgGenPass
extern llvm::cl::opt<std::string> OutputSourceFile;
extern llvm::cl::opt<std::string> OutputHeaderFile;
extern llvm::cl::opt<std::string> OutputEnabledFile;
extern llvm::cl::opt<bool> ErrorOnTranslationFailure;
extern llvm::cl::opt<bool> StaticOutput;
extern llvm::cl::opt<bool> AllowDeclCall;
extern llvm::cl::opt<bool> ForwardContext;
extern llvm::cl::opt<bool> EmitVectorPreamble;
