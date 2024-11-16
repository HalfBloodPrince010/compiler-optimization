/**
 * @file Loop Invariant Code Motion
 */
#include <llvm/Analysis/LoopPass.h>
#include <llvm/Analysis/ValueTracking.h>
#include <llvm/IR/Dominators.h>

using namespace llvm;

namespace {

class LoopInvariantCodeMotion final : public LoopPass {
public:
  static char ID;

  LoopInvariantCodeMotion() : LoopPass(ID) {}

  virtual void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<DominatorTreeWrapperPass>();
    AU.addRequired<LoopInfoWrapperPass>();
    AU.setPreservesCFG();
  }

  virtual bool runOnLoop(Loop *L, LPPassManager &LPM) override {
    errs() << "Analyzing loop in function: "
           << *(L->getHeader()) << "\n";
    for (auto *BB : L->getBlocks()) {
      errs() << "  Basic Block:" << *BB << "\n";
    }
    return false;
  }
};

char LoopInvariantCodeMotion::ID = 0;
RegisterPass<LoopInvariantCodeMotion> X("loop-invariant-code-motion",
                                        "Loop Invariant Code Motion");

} // anonymous namespace
