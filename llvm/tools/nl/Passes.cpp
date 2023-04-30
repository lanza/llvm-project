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
      auto op = dyn_cast<BinaryOperator>(&insn);
      if (!op)
        continue;
      if (op->getOpcode() != llvm::Instruction::Add)
        continue;
      auto lhs = op->getOperand(0);
      auto lhsConstant = dyn_cast<ConstantInt>(lhs);
      if (!lhsConstant)
        continue;
      auto rhs = op->getOperand(1);
      auto rhsConstant = dyn_cast<ConstantInt>(rhs);
      if (!rhsConstant)
        continue;
      // Probably doesn't do the right thing with regards to wrapping
      auto replace = ConstantInt::get(
          lhs->getType(), lhsConstant->getValue() + rhsConstant->getValue());
      insn.replaceAllUsesWith(replace);
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

void realMain(Module &module) {
  nl::ModulePassManager mpm;

  mpm.add(nl::BasicBlockToModuleProxyPass(nl::TriviallyFoldConstantAddPass()));

  mpm.run(module);
}
