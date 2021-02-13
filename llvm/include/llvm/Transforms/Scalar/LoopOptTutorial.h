#pragma once

#include "llvm/Analysis/LoopAnalysisManager.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Transforms/Utils/ValueMapper.h"

namespace llvm {

class Loop;
class LPMUpdater;

/// This class splits the innermost loop in a loop nest in the middle
class LoopSplit {
public:
  LoopSplit(LoopInfo &LI, ScalarEvolution &SE, DominatorTree &DT)
      : LI(LI), SE(SE), DT(DT) {}

  /// Execute the transformation on the loop nest rooted by \p L.
  bool run(Loop &L) const;

private:
  /// Determines if \p L is a candidate for splitting
  bool isCandidate(const Loop &L) const;

  /// Split the given loop in the middle by creating a new loop that traverse
  /// the first half of the original iteration space and adjusting the loop
  /// bounds of \p L to traverse the remaining half.
  /// Note: \p L is expected to be the innermost loop in a loop nest or a top
  /// level loop.
  bool splitLoopInHalf(Loop &L) const;

  /// Clone loop \p L and insert the cloned loop befor the basic block \p
  /// InsertBefore, \p Pres is the predeccsor of \p L.
  /// Note: \p L is expected to be the innermost loop in a loop nest or a top
  /// level loop.
  Loop *cloneLoop(Loop &L, BasicBlock &InsertBefore, BasicBlock &Pred) const;

  /// Compute the point where to split the loop \p L. Return the instruction
  /// calculating the split point.
  Instruction *computeSplitPoint(const Loop &L,
                                 Instruction *InsertBefore) const;

  /// Get the latch comparison instruction of loop \p L.
  ICmpInst *getLatchCmpInst(const Loop &L) const;

  /// Update the dominator tree after cloning the loop.
  void updateDominatorTree(const Loop &OrigLoop, const Loop &ClonedLoop,
                           BasicBlock &InsertBefore, BasicBlock &Pred,
                           ValueToValueMapTy &VMap) const;

  void dumpFunction(const StringRef Msg, const Function &F) const;

private:
  LoopInfo &LI;
  ScalarEvolution &SE;
  DominatorTree &DT;
};

class LoopOptTutorialPass : public PassInfoMixin<LoopOptTutorialPass> {
public:
  PreservedAnalyses run(Loop &L, LoopAnalysisManager &AM,
                        LoopStandardAnalysisResults &AR, LPMUpdater &U);
};

} // namespace llvm
