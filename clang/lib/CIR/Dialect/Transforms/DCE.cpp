//===- DCE.cpp -
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "PassDetail.h"
#include "mlir/Analysis/CallGraph.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/Region.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Mangle.h"
#include "clang/Basic/Module.h"
#include "clang/CIR/Dialect/Builder/CIRBaseBuilder.h"
#include "clang/CIR/Dialect/IR/CIRDialect.h"
#include "clang/CIR/Dialect/IR/CIROpsEnums.h"
#include "clang/CIR/Dialect/Passes.h"
#include "clang/CIR/Interfaces/ASTAttrInterfaces.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/Path.h"
#include <unordered_set>

using namespace mlir;
using namespace mlir::cir;

namespace {

struct DCEPass : public DCEBase<DCEPass> {
  DCEPass() = default;
  void runOnOperation() override;

  ModuleOp theModule;
};
} // namespace

void DCEPass::runOnOperation() {
  this->theModule = llvm::dyn_cast<mlir::ModuleOp>(getOperation());

  bool changed = false;
  do {
    changed = false;
    llvm::DenseSet<FuncOp> eraseList;
    for (auto &lib : theModule) {
      auto asLibrary = llvm::dyn_cast<LibraryOp>(lib);
      for (auto &element : asLibrary) {
        auto fn = llvm::dyn_cast<FuncOp>(element);
        if (!fn)
          continue;

        if (fn.getJni())
          continue;

        if (!fn.isDeclaration() &&
            fn->getAttr("linkage") !=
                GlobalLinkageKindAttr::get(fn->getContext(),
                                           GlobalLinkageKind::PrivateLinkage))
          continue;

        auto uses_or_none = SymbolTable::getSymbolUses(fn, fn->getParentOp());

        assert(uses_or_none.has_value());
        auto uses = uses_or_none.value();

        if (uses.begin() == uses.end()) {
          eraseList.insert(fn);
          changed = true;
        }
      }
    }
    llvm::for_each(eraseList, [](auto fn) { fn.erase(); });
  } while (changed);
}

std::unique_ptr<Pass> mlir::createDCEPass() {
  return std::make_unique<DCEPass>();
}
