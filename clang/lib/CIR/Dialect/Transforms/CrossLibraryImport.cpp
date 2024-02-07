//===- CrossLibraryImport.cpp - Import declared fns from other cir.library
//-===//
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
void moveFunctionCrossLibrary(mlir::cir::FuncOp &declaration,
                              mlir::cir::FuncOp &definition) {

  auto library = llvm::dyn_cast<LibraryOp>(declaration->getParentOp());

  auto *clone = definition->clone();

  library.insert(&library.front(), clone);
  declaration->replaceAllUsesWith(clone);
  declaration->erase();

  clone->setAttr("linkage",
                 GlobalLinkageKindAttr::get(clone->getContext(),
                                            GlobalLinkageKind::PrivateLinkage));

  assert(definition->use_begin() == definition->use_end());

  // definition->erase();
}

struct CrossLibraryImportPass
    : public CrossLibraryImportBase<CrossLibraryImportPass> {
  CrossLibraryImportPass() = default;
  void runOnOperation() override;

  ModuleOp theModule;

private:
  std::map<llvm::StringRef, FuncOp> fnNameToFuncOpMap;
  std::map<FuncOp, FuncOp> declToDefMap;
  std::map<FuncOp, llvm::SmallVector<FuncOp, 4>> defToDeclMap;
  std::map<llvm::StringRef, llvm::DenseSet<FuncOp>>
      declarationNameToDeclFuncOps;
};
} // namespace

void internalizeFunctions() {}

static void computeDeclarationMaps(
    mlir::ModuleOp &theModule,
    std::map<llvm::StringRef, FuncOp> &fnNameToFuncOpMap,
    std::map<FuncOp, FuncOp> &declToDefMap,
    std::map<FuncOp, llvm::SmallVector<FuncOp, 4>> &defToDeclMap,
    std::map<llvm::StringRef, llvm::DenseSet<FuncOp>>
        &declarationNameToDeclFuncOps) {

  fnNameToFuncOpMap.clear();
  declToDefMap.clear();
  defToDeclMap.clear();
  declarationNameToDeclFuncOps.clear();

  std::vector<FuncOp> declarations;

  for (auto &lib : theModule) {
    auto asLibrary = llvm::dyn_cast<mlir::cir::LibraryOp>(lib);
    for (auto &fn : asLibrary) {
      auto asFn = llvm::dyn_cast<mlir::cir::FuncOp>(fn);
      if (!asFn)
        continue;

      if (asFn.isDeclaration()) {
        declarations.push_back(asFn);
      } else {
        fnNameToFuncOpMap.insert({asFn.getName(), asFn});
        defToDeclMap.insert({asFn, {}});
      }
    }
  }

  for (auto &decl : declarations) {
    auto &def = fnNameToFuncOpMap[decl.getName()];
    declToDefMap.insert({decl, def});

    defToDeclMap[def].push_back(decl);

    auto name = decl.getName();

    if (!declarationNameToDeclFuncOps.count(name)) {
      declarationNameToDeclFuncOps.insert({name, {}});
    }

    declarationNameToDeclFuncOps[name].insert(decl);
  }
}

void CrossLibraryImportPass::runOnOperation() {
  this->theModule = llvm::dyn_cast<mlir::ModuleOp>(getOperation());
  computeDeclarationMaps(theModule, fnNameToFuncOpMap, declToDefMap,
                         defToDeclMap, declarationNameToDeclFuncOps);

  for (auto &[constDeclaration, definition] : declToDefMap) {

    auto declaration = *const_cast<FuncOp *>(&constDeclaration);

    mlir::CallGraph defCallGraph(definition->getParentOp());
    auto *result = defCallGraph.lookupNode(definition.getCallableRegion());

    auto uses_or_none =
        SymbolTable::getSymbolUses(declaration, declaration->getParentOp());

    // If the declaration has no uses we don't need to import it
    if (uses_or_none.has_value()) {
      auto &value = uses_or_none.value();
      if (value.begin() == value.end())
        continue;
    }

    // Don't move if the definition references any other symbols (e.g.
    // funcs/globals). TODO: relax this
    if (result->begin() != result->end())
      continue;

    auto checkIfCalled = [](auto &definition, auto &callGraph) {
      for (auto *node : callGraph) {
        for (auto &call : *node) {
          if (call.getTarget()->isExternal())
            continue;
          if (call.getTarget()->getCallableRegion()->getParentOp() ==
              definition)
            return true;
        }
      }
      return false;
    };

    // Don't move if the definition has any callers within that library
    // TODO: do this if profitable
    if (checkIfCalled(definition, defCallGraph))
      continue;

    // Only import fns that aren't used int heir parent for now
    if (definition->use_begin() == definition->use_end()) {
      declarationNameToDeclFuncOps[declaration.getName()].erase(declaration);
      moveFunctionCrossLibrary(*const_cast<FuncOp *>(&declaration), definition);
      // TODO: if we need a decl in the old library we'll need to add it here
    }
  }

  computeDeclarationMaps(theModule, fnNameToFuncOpMap, declToDefMap,
                         defToDeclMap, declarationNameToDeclFuncOps);

  for (auto &[constDef, decls] : defToDeclMap) {
    auto def = *const_cast<FuncOp *>(&constDef);
    if (decls.empty()) {
      def->setAttr("linkage",
                   GlobalLinkageKindAttr::get(
                       def->getContext(), GlobalLinkageKind::PrivateLinkage));
    }
  }
}

std::unique_ptr<Pass> mlir::createCrossLibraryImportPass() {
  return std::make_unique<CrossLibraryImportPass>();
}
