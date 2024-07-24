#include <llvm-12/llvm/IR/InstrTypes.h>
#include <llvm-12/llvm/IR/Instruction.h>
#include <llvm-12/llvm/Support/Casting.h>
#include <llvm/IR/Module.h>
#include <llvm/Pass.h>

using namespace llvm;

namespace {

class MultiInstOpt final : public FunctionPass {
public:
  static char ID;

  MultiInstOpt() : FunctionPass(ID) {}

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
          case Instruction::Add: {
            if (auto *operand0Instr = dyn_cast<BinaryOperator>(lhs)) {
              // c = a + t, a = b - t
              // lhs is "a", and its a BinaryOp
              Value *operand0InstrLhs = operand0Instr->getOperand(0);
              Value *operand0InstrRhs = operand0Instr->getOperand(1);
              if ((operand0Instr->getOpcode() == Instruction::Sub) &&
                  (operand0InstrRhs == rhs)) {
                I.replaceAllUsesWith(operand0InstrLhs);
                deadCodePresent = true;
                deadInstructionList.push_back(&I);
                /*
                errs() << "Instruction: " << I << "\n";
                errs() << "Instruction LHS: " << *lhs << "\n";
                errs() << "Instruction RHS: " << *rhs << "\n";
                errs() << "==================" << "\n";
                errs() << "Operand-0 Instruction: " << *operand0Instr << "\n";
                errs() << "Operand-0 Instruction LHS: " << *operand0InstrLhs
                       << "\n";
                errs() << "Operand-0 Instruction RHS: " << *operand0InstrRhs
                       << "\n";
                */
                continue;
              }
            }
            if (auto *operand1Instr = dyn_cast<BinaryOperator>(rhs)) {
              // c = t + a, a = b - t
              Value *operand1InstrLhs = operand1Instr->getOperand(0);
              Value *operand1InstrRhs = operand1Instr->getOperand(1);
              if ((operand1Instr->getOpcode() == Instruction::Sub) &&
                  (lhs == operand1InstrRhs)) {
                I.replaceAllUsesWith(operand1InstrLhs);
                deadCodePresent = true;
                deadInstructionList.push_back(&I);
              }
            }
          } break;
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
}; // class MultiInstOpt

char MultiInstOpt::ID = 0;
RegisterPass<MultiInstOpt> X("multi-inst-opt",
                             "CSCD70: Multi-Instruction Optimization");

} // anonymous namespace
