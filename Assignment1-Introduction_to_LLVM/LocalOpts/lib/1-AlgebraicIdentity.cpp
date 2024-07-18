#include "llvm/IR/InstrTypes.h"
#include <llvm-12/llvm/IR/Constants.h>
#include <llvm-12/llvm/IR/Instruction.h>
#include <llvm-12/llvm/Support/Casting.h>
#include <llvm/IR/Module.h>
#include <llvm/Pass.h>
#include <vector>

using namespace llvm;

namespace {

class AlgebraicIdentity final : public FunctionPass {
public:
  static char ID;

  AlgebraicIdentity() : FunctionPass(ID) {}

  virtual void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesAll();
  }

  void removeDeadCode(const std::vector<Instruction *> &deadInstructions) {
    for (auto &deadInst : deadInstructions) {
      if (deadInst->isSafeToRemove()) {
        deadInst->eraseFromParent();
      }
    }
  }

  virtual bool runOnFunction(Function &F) override {
    // Vector of Instruction Pointers to maintain dead code after Algebraic
    // Identity
    std::vector<Instruction *> deadInstructionList;
    // flag to check if Algebraic Identity resulted in deadcode. If yes, then
    // we will need to deadcode elimination.
    bool deadCodePresent = false;
    for (auto &B : F) {
      for (auto &I : B) {
        if (auto *op = dyn_cast<BinaryOperator>(&I)) {
          Value *lhs = op->getOperand(0);
          Value *rhs = op->getOperand(1);
          switch (op->getOpcode()) {
          case Instruction::Add: {
            if (auto *constOp1 = dyn_cast<ConstantInt>(lhs)) {
              if (constOp1->isZero()) {
                // 0 + x
                I.replaceAllUsesWith(rhs);
                deadInstructionList.push_back(&I);
                deadCodePresent = true;
              }
            } else if (auto *constOp2 = dyn_cast<ConstantInt>(rhs)) {
              if (constOp2->isZero()) {
                // x + 0
                I.replaceAllUsesWith(lhs);
                deadInstructionList.push_back(&I);
                deadCodePresent = true;
              }
            }
            break;
          }
          case Instruction::Mul: {
            if (auto *constOp1 = dyn_cast<ConstantInt>(lhs)) {
              if (constOp1->isOne() || constOp1->isZero()) {
                if (constOp1->isOne()) {
                  // 1 * x;
                  I.replaceAllUsesWith(rhs);
                } else if (constOp1->isZero()) {
                  // 0 * x;
                  I.replaceAllUsesWith(lhs);
                }
                deadInstructionList.push_back(&I);
                deadCodePresent = true;
              }
            } else if (auto *constOp2 = dyn_cast<ConstantInt>(rhs)) {
              if (constOp2->isOne() || constOp2->isZero()) {
                if (constOp2->isOne()) {
                  // x * 1
                  I.replaceAllUsesWith(lhs);
                } else if (constOp2->isZero()) {
                  // x * 0
                  I.replaceAllUsesWith(rhs);
                }
                deadInstructionList.push_back(&I);
                deadCodePresent = true;
              }
            }
            break;
          }
          case Instruction::SDiv: {
            if (auto *constOp2 = dyn_cast<ConstantInt>(rhs)) {
              if (constOp2->isOne()) {
                // x / 1
                I.replaceAllUsesWith(lhs);
                deadInstructionList.push_back(&I);
                deadCodePresent = true;
              }
            }
            break;
          }
          case Instruction::Sub: {
            if (auto *constOp2 = dyn_cast<ConstantInt>(rhs)) {
              if (constOp2->isZero()) {
                // x - 0
                I.replaceAllUsesWith(lhs);
                deadInstructionList.push_back(&I);
                deadCodePresent = true;
              }
            }
            break;
          }
          default: {
            break;
          }
          }
        }
      }
    }
    if (deadCodePresent) {
      removeDeadCode(deadInstructionList);
      // Code modified
      return true;
    }
    return false;
  }
}; // class AlgebraicIdentity

char AlgebraicIdentity::ID = 0;
RegisterPass<AlgebraicIdentity> X("algebraic-identity",
                                  "CSCD70: Algebraic Identity");

} // anonymous namespace
