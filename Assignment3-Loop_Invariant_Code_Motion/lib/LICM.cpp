/**
 * @file Loop Invariant Code Motion
 */
#include <algorithm>
#include <llvm-12/llvm/IR/BasicBlock.h>
#include <llvm-12/llvm/IR/Instruction.h>
#include <llvm-12/llvm/Support/Casting.h>
#include <llvm-12/llvm/Support/raw_ostream.h>
#include <llvm/Analysis/LoopPass.h>
#include <llvm/Analysis/ValueTracking.h>
#include <llvm/IR/Dominators.h>
#include <vector>

using namespace llvm;

namespace {

class LoopInvariantCodeMotion final : public LoopPass {
public:
  static char ID;
  std::vector<Instruction *> invariantInstructions;

  LoopInvariantCodeMotion() : LoopPass(ID) {}

  virtual void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<DominatorTreeWrapperPass>();
    AU.addRequired<LoopInfoWrapperPass>();
    AU.setPreservesCFG();
  }

  bool isInvariant(Instruction *const I, Loop *L) {
    /*
    Loop Invariant has following conditions

      1. Definitions are defined outside the loop.
      2. There is only 1 definition, and that is from loop invariant instruction
    within the loop.
      3. Constant/Argument

      Note: In SSA Form, there is always only 1 definition.
    */
    bool IsInvariant = true;
    for (auto &operand : I->operands()) {
      Value *value = operand.get();
      // Condition 3: Constant or Argument
      if (!isa<Constant>(value) && !isa<Argument>(value)) {
        if (Instruction *OpInst = dyn_cast<Instruction>(value)) {
          bool isInstOperandInvariant =
              std::find(invariantInstructions.begin(),
                        invariantInstructions.end(),
                        OpInst) != invariantInstructions.end();
          
          BasicBlock *OpInstBasicBlock = OpInst->getParent();
          // Condition 2: If Instruction is defined in Loop, but not invariant
          if (L->contains(OpInstBasicBlock) && !isInstOperandInvariant) {
            return false;
          }
          // Condition 1: Instruction defined outside the loop, already handled by default=true case.
        }
      }
    }

    return isSafeToSpeculativelyExecute(I) && !I->mayReadFromMemory() &&
           !isa<LandingPadInst>(I) && IsInvariant;
  }

  bool dominatesAllLoopExit(Loop *L, DominatorTree *DT, Instruction *I) {
    bool dominatesAllExits = true;
    llvm::SmallVector<llvm::BasicBlock *, 8> ExitBlocks;
    L->getExitBlocks(ExitBlocks);

    for(auto *ExitBB: ExitBlocks) {
      if(!DT->dominates(I->getParent(), ExitBB)) {
        return false;
      }
    }

    return dominatesAllExits;
  }

  virtual bool runOnLoop(Loop *L, LPPassManager &LPM) override {
    // For each loop, clear the list of invariantInstructions.
    invariantInstructions.clear();
    bool hasChanged = false;

    if (!L->getLoopPreheader()) {
      errs() << "Loop Preheader not inserted!" << "\n";
      }
    errs() << "Loop Preheader before Code Motion: " << *(L->getLoopPreheader()) << "\n";

    // clang-format off
    errs() << "**************************************************" << "\n"
           << "* Invariant Instructions" << "\n"
           << "**************************************************" << "\n";
    // clang-format on

    errs() << "Analyzing loop in function: " << *(L->getHeader()) << "\n";
    for (auto *BB : L->getBlocks()) {
      errs() << "  Basic Block:" << *BB << "\n";
      for (auto &I : *BB) {
        bool notMarkedInvariant = std::find(invariantInstructions.begin(),
                                            invariantInstructions.end(),
                                            &I) == invariantInstructions.end();
        if (isInvariant(&I, L) && notMarkedInvariant) {
          errs() << "    Invariant Instruction:" << I << "\n";
          invariantInstructions.emplace_back(&I);
          hasChanged = true;
        }
      }
      errs() << "\n\n";
    }

    DominatorTreeWrapperPass &dominatorTreePass = getAnalysis<DominatorTreeWrapperPass>();
    DominatorTree &DT = dominatorTreePass.getDomTree();

    /*
    Code Motion Conditions
      1. Loop Invariant Instruction
      2. Dominates all the exits in the Loop
      3. Assigned only once.
      4. Definition dominates the use.

      Note: SSA handles condition 3 and 4
    */

    // clang-format off
    errs() << "**************************************************" << "\n"
           << "* Code Motion" << "\n"
           << "**************************************************" << "\n";
    // clang-format on

    for(auto *invariantInst: invariantInstructions){
      if(dominatesAllLoopExit(L, &DT, invariantInst)) {
        //TODO: Move to Preheader
        errs() << "Instruction to Move to the preheader:" << *invariantInst << "\n";
        invariantInst->moveBefore(L->getLoopPreheader()->getTerminator());
        errs() << "\n\n";
      }
    }

    errs() << "Loop Preheader after Code Motion: " << *(L->getLoopPreheader()) << "\n";

    return hasChanged;
  }
};

char LoopInvariantCodeMotion::ID = 0;
RegisterPass<LoopInvariantCodeMotion> X("loop-invariant-code-motion",
                                        "Loop Invariant Code Motion");

} // anonymous namespace
