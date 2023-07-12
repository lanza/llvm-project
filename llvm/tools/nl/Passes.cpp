#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/InstrTypes.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Module.h>
#include <llvm/Transforms/Utils/Cloning.h>

#include <llvm/IR/CFG.h>

using namespace llvm;

namespace nl {
class ModulePass {
public:
  ModulePass() = default;
  virtual void run(Module &module) = 0;
  virtual ~ModulePass() = default;
  ModulePass(ModulePass const &) = delete;
  ModulePass(ModulePass &&) = default;
  auto operator=(ModulePass const &) -> ModulePass & = delete;
  auto operator=(ModulePass &&) -> ModulePass & = default;
};
class FunctionPass {
public:
  FunctionPass() = default;
  virtual void run(Function &function) = 0;
  virtual ~FunctionPass() = default;
  FunctionPass(FunctionPass const &) = delete;
  FunctionPass(FunctionPass &&) = default;
  auto operator=(FunctionPass const &) -> FunctionPass & = delete;
  auto operator=(FunctionPass &&) -> FunctionPass & = default;
};
class BasicBlockPass {
public:
  BasicBlockPass() = default;
  virtual void run(BasicBlock &block) = 0;
  virtual ~BasicBlockPass() = default;
  BasicBlockPass(BasicBlockPass const &) = delete;
  BasicBlockPass(BasicBlockPass &&) = default;
  auto operator=(BasicBlockPass const &) -> BasicBlockPass & = delete;
  auto operator=(BasicBlockPass &&) -> BasicBlockPass & = default;
};
class BasicBlockPassManager : public FunctionPass {
  std::vector<std::unique_ptr<BasicBlockPass>> passes;

public:
  void add(BasicBlockPass *pass) {
    passes.push_back(std::unique_ptr<BasicBlockPass>(pass));
  }
  template <typename T> void add(T pass) {
    passes.push_back(std::make_unique<T>(std::move(pass)));
  }
  void run(Function &function) override {
    for (auto &pass : passes)
      for (auto &block : function)
        pass->run(block);
  }
};
class FunctionPassManager : public ModulePass {
  std::vector<std::unique_ptr<FunctionPass>> passes;

public:
  void add(FunctionPass *pass) {
    passes.push_back(std::unique_ptr<FunctionPass>(pass));
  }
  template <typename T> void add(T pass) {
    passes.push_back(std::make_unique<T>(std::move(pass)));
  }
  void run(Module &module) override {
    for (auto &pass : passes)
      for (auto &function : module)
        pass->run(function);
  }
};
class FunctionToModuleProxyPass : public ModulePass {
  std::unique_ptr<FunctionPass> pass;

public:
  template <typename T>
  explicit FunctionToModuleProxyPass(T pass)
      : pass{std::make_unique<T>(std::move(pass))} {}
  void run(Module &module) override {
    for (auto &function : module)
      pass->run(function);
  }
};
class BasicBlockToModuleProxyPass : public ModulePass {
  std::unique_ptr<BasicBlockPass> pass;

public:
  template <typename T>
  explicit BasicBlockToModuleProxyPass(T pass)
      : pass{std::make_unique<T>(std::move(pass))} {}
  void run(Module &module) override {
    for (auto &function : module)
      for (auto &block : function)
        pass->run(block);
  }
};

class ModulePassManager : public ModulePass {
  std::vector<std::unique_ptr<ModulePass>> passes;

public:
  void add(std::unique_ptr<ModulePass> pass) {
    passes.push_back(std::move(pass));
  }
  void add(ModulePass *pass) {
    passes.push_back(std::unique_ptr<ModulePass>(pass));
  }
  template <typename T> void add(T pass) {
    passes.push_back(std::make_unique<T>(std::move(pass)));
  }
  void run(Module &module) override {
    for (auto &pass : passes)
      pass->run(module);
  }
};

class TriviallyFoldConstantAddPass : public BasicBlockPass {
public:
  void run(BasicBlock &block) override {
    for (auto &insn : block) {
      auto *binOp = dyn_cast<BinaryOperator>(&insn);
      if (binOp == nullptr)
        continue;
      if (binOp->getOpcode() != llvm::Instruction::Add)
        continue;
      auto *lhs = binOp->getOperand(0);
      auto *lhsConstant = dyn_cast<ConstantInt>(lhs);
      if (lhsConstant == nullptr)
        continue;
      auto *rhs = binOp->getOperand(1);
      auto *rhsConstant = dyn_cast<ConstantInt>(rhs);
      if (rhsConstant == nullptr)
        continue;
      // Probably doesn't do the right thing with regards to wrapping
      auto *replace = ConstantInt::get(
          lhs->getType(), lhsConstant->getValue() + rhsConstant->getValue());
      insn.replaceAllUsesWith(replace);
    }
  }
};

class TriviallyFoldAddZeroPass : public BasicBlockPass {
public:
  void run(BasicBlock &block) override {
    for (auto &insn : block) {
      auto *binOp = dyn_cast<BinaryOperator>(&insn);
      if (binOp == nullptr)
        continue;
      if (binOp->getOpcode() != llvm::Instruction::Add)
        continue;
      auto *lhs = binOp->getOperand(0);
      auto *rhs = binOp->getOperand(1);

      if (auto *lhsConstant = dyn_cast<ConstantInt>(lhs)) {
        if (lhsConstant->getValue().isZero()) {
          insn.replaceAllUsesWith(rhs);
          continue;
        }
      }

      if (auto *rhsConstant = dyn_cast<ConstantInt>(rhs)) {
        if (rhsConstant->getValue().isZero()) {
          insn.replaceAllUsesWith(lhs);
          continue;
        }
      }
    }
  }
};

class RemoveDeadInstructionPass : public BasicBlockPass {
public:
  void run(BasicBlock &block) override {
    for (auto &insn : make_early_inc_range(block)) {
      // TODO(lanza): is this right?
      if (insn.isTerminator())
        continue;
      if (insn.users().begin() == insn.users().end())
        insn.eraseFromParent();
    }
  }
};

class Inliner : public ModulePass {
  void run(Module &module) override {
    for (auto &func : module) {
      for (auto &block : func) {
        for (auto &insn : make_early_inc_range(block)) {
          if (auto *call = llvm::dyn_cast<CallInst>(&insn)) {
            InlineFunctionInfo inlineInfo;
            InlineFunction(*call, inlineInfo);
          }
        }
      }
    }
  }
};

class PlayWithCFGPass : public FunctionPass {
public:
  void run(Function &function) override {
    auto &entry = function.getEntryBlock();

    // for (auto *pred : predecessors(&entry)) {
    //   pred->dump();
    // }
    // for (auto *succ : successors(&entry)) {
    //   succ->dump();
    // }

    // for (auto child = GraphTraits<BasicBlock *>::child_begin(&entry);
    //      child != GraphTraits<BasicBlock *>::child_end(&entry); child++) {
    //   child->dump();
    // }

    for (auto *node : nodes(&function)) {
      node->dump();
    }
    // for (auto *node : nodes(&entry)) {
    //   node->dump();
    // }
  }
};

class PrintInstructionPass : public ModulePass {
public:
  void run(Module &module) override {
    for (auto &function : module)
      for (auto &block : function)
        for (auto &insn : block) {
#ifdef DEBUG
          insn.dump();
#else
          outs() << insn.getName() << '\n';
#endif
        }
  }
};
} // namespace nl

auto splitPasses(std::string_view passPipeline)
    -> std::vector<std::string_view> {

  if (passPipeline.empty())
    return {};

  size_t index = 0;
  size_t previousIndex = 0;

  std::vector<std::string_view> elements;

  using namespace std::literals;

  while (index = passPipeline.find(","sv, previousIndex),
         index != std::string::npos) {
    elements.emplace_back(passPipeline.begin() +
                              static_cast<int>(previousIndex),
                          passPipeline.begin() + static_cast<int>(index));
    previousIndex = index + 1;
  }

  if (previousIndex <= passPipeline.size())
    elements.emplace_back(
        passPipeline.begin() + static_cast<int>(previousIndex),
        passPipeline.begin() + static_cast<int>(passPipeline.size()));

  return elements;
}

auto mapStringToPasses(const std::vector<std::string_view> &passes)
    -> std::vector<std::unique_ptr<nl::ModulePass>> {
  using namespace std::literals;
  std::vector<std::unique_ptr<nl::ModulePass>> outPasses;
  for (auto passName : passes) {
    if (passName == "foldaddzero")
      outPasses.emplace_back(std::make_unique<nl::BasicBlockToModuleProxyPass>(
          nl::TriviallyFoldAddZeroPass()));
    else if (passName == "foldaddconstant")
      outPasses.emplace_back(std::make_unique<nl::BasicBlockToModuleProxyPass>(
          nl::TriviallyFoldConstantAddPass()));
    else if (passName == "deadinsn")
      outPasses.emplace_back(std::make_unique<nl::BasicBlockToModuleProxyPass>(
          nl::RemoveDeadInstructionPass()));
    else if (passName == "printinsn")
      outPasses.emplace_back(std::make_unique<nl::PrintInstructionPass>());
    else if (passName == "playwithcfg")
      outPasses.emplace_back(std::make_unique<nl::FunctionToModuleProxyPass>(
          nl::PlayWithCFGPass()));
    else if (passName == "inline")
      outPasses.emplace_back(std::make_unique<nl::Inliner>());
    else
      llvm_unreachable("NYI");
  }

  return outPasses;
}

void realMain(Module &module, std::string_view passPipeline) {

  auto passes = mapStringToPasses(splitPasses(passPipeline));

  nl::ModulePassManager mpm;
  for (auto &pass : passes)
    mpm.add(std::move(pass));

  mpm.run(module);
}
