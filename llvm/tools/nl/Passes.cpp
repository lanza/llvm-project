#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/InstrTypes.h>
#include <llvm/IR/Module.h>

using namespace llvm;

namespace nl {
class ModulePass {
public:
  ModulePass() {}
  virtual void run(Module &module) = 0;
  virtual ~ModulePass() {}
};
class FunctionPass {
public:
  FunctionPass() {}
  virtual void run(Function &function) = 0;
  virtual ~FunctionPass() {}
};
class BasicBlockPass {
public:
  BasicBlockPass() {}
  virtual void run(BasicBlock &block) = 0;
  virtual ~BasicBlockPass() {}
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
  virtual void run(Function &function) override {
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
  virtual void run(Module &module) override {
    for (auto &pass : passes)
      for (auto &function : module)
        pass->run(function);
  }
};
class BasicBlockToModuleProxyPass : public ModulePass {
  std::unique_ptr<BasicBlockPass> pass;

public:
  template <typename T>
  BasicBlockToModuleProxyPass(T pass)
      : pass{std::make_unique<T>(std::move(pass))} {}
  virtual void run(Module &module) override {
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
  virtual void run(Module &module) override {
    for (auto &pass : passes)
      pass->run(module);
  }
};

class TriviallyFoldConstantAddPass : public BasicBlockPass {
public:
  virtual void run(BasicBlock &block) override {
    for (auto &insn : block) {
      auto *op = dyn_cast<BinaryOperator>(&insn);
      if (!op)
        continue;
      if (op->getOpcode() != llvm::Instruction::Add)
        continue;
      auto *lhs = op->getOperand(0);
      auto *lhsConstant = dyn_cast<ConstantInt>(lhs);
      if (!lhsConstant)
        continue;
      auto *rhs = op->getOperand(1);
      auto *rhsConstant = dyn_cast<ConstantInt>(rhs);
      if (!rhsConstant)
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
  virtual void run(BasicBlock &block) override {
    for (auto &insn : block) {
      auto *op = dyn_cast<BinaryOperator>(&insn);
      if (!op)
        continue;
      if (op->getOpcode() != llvm::Instruction::Add)
        continue;
      auto *lhs = op->getOperand(0);
      auto *rhs = op->getOperand(1);

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
  virtual void run(BasicBlock &block) override {
    for (auto &insn : make_early_inc_range(block)) {
      // TODO: is this right?
      if (insn.isTerminator())
        continue;
      if (insn.users().begin() == insn.users().end())
        insn.eraseFromParent();
    }
  }
};

class PrintInstructionPass : public ModulePass {
public:
  PrintInstructionPass() {}
  virtual ~PrintInstructionPass() {}

  virtual void run(Module &module) override {
    for (auto &function : module)
      for (auto &block : function)
        for (auto &insn : block)
          insn.dump();
  }
};
} // namespace nl

std::vector<std::string_view> splitPasses(std::string_view passPipeline) {

  if (passPipeline.empty())
    return {passPipeline};

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
    elements.emplace_back(passPipeline.begin() + previousIndex,
                          passPipeline.begin() + index);

  return elements;
}

std::vector<std::unique_ptr<nl::ModulePass>>
mapStringToPasses(std::vector<std::string_view> passes) {
  using namespace std::literals;
  std::vector<std::unique_ptr<nl::ModulePass>> outPasses;
  for (auto passName : passes) {
    if (passName == "foldaddzero")
      outPasses.emplace_back(
          new nl::BasicBlockToModuleProxyPass(nl::TriviallyFoldAddZeroPass()));
    else if (passName == "foldaddconstant")
      outPasses.emplace_back(new nl::BasicBlockToModuleProxyPass(
          nl::TriviallyFoldConstantAddPass()));
    else if (passName == "deadinsn")
      outPasses.emplace_back(
          new nl::BasicBlockToModuleProxyPass(nl::RemoveDeadInstructionPass()));
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
