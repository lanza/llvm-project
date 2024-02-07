//===- CrossLibraryImport.cpp - Import declared fns from other cir.library
//-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "PassDetail.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/Region.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Mangle.h"
#include "clang/Basic/Module.h"
#include "clang/CIR/Dialect/Builder/CIRBaseBuilder.h"
#include "clang/CIR/Dialect/IR/CIRDialect.h"
#include "clang/CIR/Dialect/Passes.h"
#include "clang/CIR/Interfaces/ASTAttrInterfaces.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/Path.h"

using namespace mlir;
using namespace mlir::cir;

namespace {
struct CrossLibraryImportPass
    : public CrossLibraryImportBase<CrossLibraryImportPass> {
  CrossLibraryImportPass() = default;
  void runOnOperation() override;

  ModuleOp theModule;
};
} // namespace

void CrossLibraryImportPass::runOnOperation() {}

std::unique_ptr<Pass> mlir::createCrossLibraryImportPass() {
  return std::make_unique<CrossLibraryImportPass>();
}
