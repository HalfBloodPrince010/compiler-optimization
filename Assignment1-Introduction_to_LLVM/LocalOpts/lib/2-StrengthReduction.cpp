#include "llvm/IR/IRBuilder.h"
#include <llvm-12/llvm/IR/Constants.h>
#include <llvm-12/llvm/IR/InstrTypes.h>
#include <llvm-12/llvm/IR/Instruction.h>
#include <llvm-12/llvm/Support/Casting.h>
#include <llvm/IR/Module.h>
#include <llvm/Pass.h>
#include <vector>

using namespace llvm;

// returns the power value if power of 2, else returns 0
int isPowerofTwo(int n) {
  if (n == 0)
    return 0;

  int power = 0;
  while (n != 1) {
    if (n % 2 != 0)
      return 0;
    power++;
    n = n / 2;
  }

  return power;
}

namespace {

class StrengthReduction final : public FunctionPass {
public:
  static char ID;

  StrengthReduction() : FunctionPass(ID) {}

  virtual void getAnalysisUsage(AnalysisUsage &AU) const override {}

  void removeDeadCode(const std::vector<Instruction *> &deadInstructions) {
    for (auto &deadInst : deadInstructions) {
      if (deadInst->isSafeToRemove()) {
        deadInst->eraseFromParent();
      }
    }
  }

  virtual bool runOnFunction(Function &F) override {
    std::vector<Instruction *> deadInstructionList;
    bool deadCodePresent = false;
    for (auto &B : F) {
      for (auto &I : B) {
        if (auto *op = dyn_cast<BinaryOperator>(&I)) {
          Value *lhs = op->getOperand(0);
          Value *rhs = op->getOperand(1);
          switch (op->getOpcode()) {
          case Instruction::Mul: {
            if (auto *constOperand0 = dyn_cast<ConstantInt>(lhs)) {
              // 8 * x
              int power = isPowerofTwo(constOperand0->getSExtValue());
              if (power > 0) {
                // Insert at the point "before" where the instruction `op`
                // appears.
                // https://llvm.org/docs/ProgrammersManual.html#creating-and-inserting-new-instructions
                IRBuilder<> builder(op);
                Value *newLeftShiftInst = builder.CreateLShr(rhs, power);
                I.replaceAllUsesWith(newLeftShiftInst);
                deadCodePresent = true;
                deadInstructionList.push_back(&I);
              }
            } else if (auto *constOperand1 = dyn_cast<ConstantInt>(rhs)) {
              // x * 8
              int power = isPowerofTwo(constOperand1->getSExtValue());
              if (power > 0) {
                IRBuilder<> builder(op);
                Value *newLeftShiftInst = builder.CreateLShr(lhs, power);
                I.replaceAllUsesWith(newLeftShiftInst);
                deadCodePresent = true;
                deadInstructionList.push_back(&I);
              }
            }
            break;
          }
          case Instruction::SDiv: {
            if (auto *constOperand1 = dyn_cast<ConstantInt>(rhs)) {
              int power = isPowerofTwo(constOperand1->getSExtValue());
              if (power > 0) {
                IRBuilder<> builder(op);
                Value *newRightShiftInst = builder.CreateAShr(lhs, power);
                I.replaceAllUsesWith(newRightShiftInst);
                deadCodePresent = true;
                deadInstructionList.push_back(&I);
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
}; // class StrengthReduction

char StrengthReduction::ID = 0;
RegisterPass<StrengthReduction> X("strength-reduction",
                                  "CSCD70: Strength Reduction");

} // anonymous namespace
