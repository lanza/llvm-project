#include "llvm/Transforms/Scalar/LoopOptTutorial.h"
#include "llvm/Analysis/IVDescriptors.h"
#include "llvm/Analysis/LoopPass.h"
#include "llvm/IR/Function.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Utils/ValueMapper.h"

using namespace llvm;

#define DEBUG_TYPE "loop-opt-tutorial"
static const char *VerboseDebug = DEBUG_TYPE "-verbose";

/// Clones a loop \p OrigLoop. Returns the loop and the blocks in \p Blocks.
/// Updates LoopInfo assuming the loop is dominated by block \p LoopDomBB.
/// Insert the new blocks before block specified in \p Before.
static Loop *myCloneLoopWithPreheader(BasicBlock *Before, BasicBlock *LoopDomBB,
                                      Loop *OrigLoop, ValueToValueMapTy &VMap,
                                      const Twine &NameSuffix, LoopInfo *LI,
                                      SmallVectorImpl<BasicBlock *> &Blocks);

//===-----------------------===//
// LoopSplit implementation
//

bool LoopSplit::run(Loop &L) const {
  LLVM_DEBUG(dbgs() << "Entering LoopSplit::run\n");

  if (!isCandidate(L)) {

    LLVM_DEBUG(dbgs() << "Loop " << L.getName()
                      << " is not a candidate for splitting.\n");

    return false;
  }

  return splitLoopInHalf(L);
}

bool LoopSplit::isCandidate(const Loop &L) const {
  // Require loops with preheaders and dedicated exits.
  if (!L.isLoopSimplifyForm())
    return false;

  // Since we are cloning to split the loop, it has to be safe to clone.
  if (!L.isSafeToClone())
    return false;

  // If loop has multiple exit blocks, do not split.
  if (!L.getExitingBlock())
    return false;

  // Only split innermost loops. Thus, if the loop has any children, it cannot
  // be split.
  if (!L.getSubLoops().empty())
    return false;

  return true;
}

bool LoopSplit::splitLoopInHalf(Loop &L) const {
  assert(L.isLoopSimplifyForm() && "Expecting a loop in simplify form");
  assert(L.isSafeToClone() && "Loop is not safe to be cloned");
  assert(L.getSubLoops().empty() && "Expecting an innermost loop");

  const Function &F = *L.getHeader()->getParent();
  LLVM_DEBUG(dbgs() << "Splitting loop " << L.getName() << "\n");

  Instruction *Split =
      computeSplitPoint(L, L.getLoopPreheader()->getTerminator());

  BasicBlock *Preheader = L.getLoopPreheader();
  BasicBlock *Pred = Preheader;
  BasicBlock *InsertBefore =
      SplitBlock(Preheader, Preheader->getTerminator(), &DT, &LI);
  DEBUG_WITH_TYPE(VerboseDebug,
                  dumpFunction("After splitting preheader:\n", F););

  Loop *ClonedLoop = cloneLoop(L, *InsertBefore, *Pred);

  ICmpInst *LatchCmpInst = getLatchCmpInst(*ClonedLoop);
  assert(LatchCmpInst && "Unable to find the latch comparison instruction");
  LatchCmpInst->setOperand(1, Split);

  PHINode *IndVar = L.getInductionVariable(SE);
  assert(IndVar && "Unable to find the induction variable PHI node");
  IndVar->setIncomingValueForBlock(L.getLoopPreheader(), Split);

  DEBUG_WITH_TYPE(VerboseDebug,
                  dumpFunction("After splitting the loop:\n", F););

  return true;
}

Loop *LoopSplit::cloneLoop(Loop &L, BasicBlock &InsertBefore,
                           BasicBlock &Pred) const {
  Function &F = *L.getHeader()->getParent();
  SmallVector<BasicBlock *, 4> ClonedLoopBlocks;
  ValueToValueMapTy VMap;

  Loop *NewLoop = myCloneLoopWithPreheader(&InsertBefore, &Pred, &L, VMap, "",
                                           &LI, ClonedLoopBlocks);

  assert(NewLoop && "Run out of memory");
  DEBUG_WITH_TYPE(VerboseDebug,
                  dbgs() << "Create new loop: " << NewLoop->getName() << "\n";
                  dumpFunction("After cloning loop:\n", F););

  VMap[L.getExitBlock()] = &InsertBefore;
  remapInstructionsInBlocks(ClonedLoopBlocks, VMap);
  DEBUG_WITH_TYPE(VerboseDebug,
                  dumpFunction("After instruction remapping:\n", F););

  Pred.getTerminator()->replaceUsesOfWith(&InsertBefore,
                                          NewLoop->getLoopPreheader());

  return NewLoop;
}

Instruction *LoopSplit::computeSplitPoint(const Loop &L,
                                          Instruction *InsertBefore) const {
  Optional<Loop::LoopBounds> Bounds = L.getBounds(SE);
  assert(Bounds.hasValue() && "Unable to retrieve the loop bounds");

  Value &IVInitialVal = Bounds->getInitialIVValue();
  Value &IVFinalVal = Bounds->getFinalIVValue();
  auto *Sub = BinaryOperator::Create(Instruction::Sub, &IVFinalVal,
                                     &IVInitialVal, "", InsertBefore);

  return BinaryOperator::Create(Instruction::UDiv, Sub,
                                ConstantInt::get(IVFinalVal.getType(), 2), "",
                                InsertBefore);
}

ICmpInst *LoopSplit::getLatchCmpInst(const Loop &L) const {
  if (BasicBlock *Latch = L.getLoopLatch())
    if (BranchInst *BI = dyn_cast_or_null<BranchInst>(Latch->getTerminator()))
      if (BI->isConditional())
        return dyn_cast<ICmpInst>(BI->getCondition());

  return nullptr;
}

void LoopSplit::dumpFunction(const StringRef Msg, const Function &F) const {
  dbgs() << Msg;
  F.dump();
}

static Loop *myCloneLoopWithPreheader(BasicBlock *Before, BasicBlock *LoopDomBB,
                                      Loop *OrigLoop, ValueToValueMapTy &VMap,
                                      const Twine &NameSuffix, LoopInfo *LI,
                                      SmallVectorImpl<BasicBlock *> &Blocks) {
  Function *F = OrigLoop->getHeader()->getParent();
  Loop *ParentLoop = OrigLoop->getParentLoop();
  DenseMap<Loop *, Loop *> LMap;

  Loop *NewLoop = LI->AllocateLoop();
  LMap[OrigLoop] = NewLoop;
  if (ParentLoop)
    ParentLoop->addChildLoop(NewLoop);
  else
    LI->addTopLevelLoop(NewLoop);

  BasicBlock *OrigPH = OrigLoop->getLoopPreheader();
  assert(OrigPH && "No preheader");
  BasicBlock *NewPH = CloneBasicBlock(OrigPH, VMap, NameSuffix, F);

  VMap[OrigPH] = NewPH;
  Blocks.push_back(NewPH);

  if (ParentLoop)
    ParentLoop->addBasicBlockToLoop(NewPH, *LI);

  for (Loop *CurLoop : OrigLoop->getLoopsInPreorder()) {
    Loop *&NewLoop = LMap[CurLoop];
    if (!NewLoop) {
      NewLoop = LI->AllocateLoop();

      Loop *OrigParent = CurLoop->getParentLoop();
      assert(OrigParent && "Coudl not find the original parent loop");
      Loop *NewParentLoop = LMap[OrigParent];
      assert(NewParentLoop && "Could not find the new parent loop");

      NewParentLoop->addChildLoop(NewLoop);
    }
  }

  for (BasicBlock *BB : OrigLoop->getBlocks()) {
    Loop *CurLoop = LI->getLoopFor(BB);
    Loop *&NewLoop = LMap[CurLoop];
    assert(NewLoop && "Expecting new loop to be allocated");

    BasicBlock *NewBB = CloneBasicBlock(BB, VMap, NameSuffix, F);
    VMap[BB] = NewBB;

    NewLoop->addBasicBlockToLoop(NewBB, *LI);
    if (BB == CurLoop->getHeader())
      NewLoop->moveToHeader(NewBB);

    Blocks.push_back(NewBB);
  }

  F->getBasicBlockList().splice(Before->getIterator(), F->getBasicBlockList(),
                                NewPH);
  F->getBasicBlockList().splice(Before->getIterator(), F->getBasicBlockList(),
                                NewLoop->getHeader()->getIterator(), F->end());

  return NewLoop;
}

PreservedAnalyses LoopOptTutorialPass::run(Loop &L, LoopAnalysisManager &LAM,
                                           LoopStandardAnalysisResults &AR,
                                           LPMUpdater &U) {
  LLVM_DEBUG(dbgs() << "Entering LoopOptTutorialPass::run\n");
  LLVM_DEBUG(dbgs() << "Loop: "; L.dump(); dbgs() << "\n");

  bool Changed = LoopSplit(AR.LI, AR.SE, AR.DT).run(L);

  if (!Changed)
    return PreservedAnalyses::all();

  return llvm::getLoopPassPreservedAnalyses();
}
