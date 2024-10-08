/**
 * @file Liveness Dataflow Analysis
 */
#include <llvm-12/llvm/IR/Argument.h>
#include <llvm-12/llvm/IR/InstrTypes.h>
#include <llvm-12/llvm/IR/Instruction.h>
#include <llvm-12/llvm/IR/User.h>
#include <llvm-12/llvm/IR/Value.h>
#include <llvm-12/llvm/Support/Casting.h>
#include <llvm/IR/Module.h>
#include <llvm/Pass.h>
#include <llvm/Support/raw_ostream.h>

#include <dfa/Framework.h>
#include <dfa/MeetOp.h>

#include "Variable.h"

// using namespace dfa;
using namespace llvm;

namespace {

using LivenessFrameworkBase =
    Framework<Variable, bool, Direction::kBackward, Union>;

class Liveness final : public LivenessFrameworkBase, public FunctionPass {
private:
  virtual void initializeDomainFromInst(const Instruction &Inst) override {
    errs() << "Instruction: " << Inst << "\n";
    for (auto &operand : Inst.operands()) {
      Value *value = operand.get();
      if (isa<Instruction>(value) || isa<Argument>(value)) {
        Variable variable = Variable(value);
#ifdef DEBUG_LIVENESS
          errs() << "\tOperand: " << *operand << "\n";
#endif 
        if (std::find(Domain.begin(), Domain.end(), variable) == Domain.end()) {
          Domain.emplace_back(variable);
        }
      }
    }
  }

  virtual bool transferFunc(const Instruction &Inst, const DomainVal_t &IBV,
                            DomainVal_t &OBV) override {

    /*
     * Step 1: Kill
     * New instruction INST defined
     * kills the previous "uses"
     */

    DomainVal_t TEMP_OBV = IBV;

    int Idx = 0;
    for (Variable variableDom : Domain) {
      if (const Value *const value = dyn_cast<Value>(&Inst)) {
        Variable variableDef = Variable(value);
        if (variableDef == variableDom) {
          TEMP_OBV.at(Idx) = false;
        }
      }

      Idx++;
    }

    /*
     * Step 2: Generate
     * For all operands of Instruction Inst (i.e uses)
     * set the BV value to true.
     */
    for (auto &operand : Inst.operands()) {
      Value *value = operand.get();
      if (isa<Instruction>(value) || isa<Argument>(value)) {
        Variable variable = Variable(value);
        auto it = std::find(Domain.begin(), Domain.end(), variable);
        if (it != Domain.end()) {
          int index = it - Domain.begin();
          TEMP_OBV.at(index) = true;
        }
      }
    }

    // Did output Bit Vector change?
    bool isChanged = TEMP_OBV != OBV;

    OBV = TEMP_OBV;
    return isChanged;
  }

public:
  static char ID;

  Liveness() : FunctionPass(ID) {}

  virtual void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesAll();
  }

  virtual bool runOnFunction(Function &F) override {
    return LivenessFrameworkBase::runOnFunction(F);
  }
};

char Liveness::ID = 0;
RegisterPass<Liveness> X("liveness", "Liveness");

} // anonymous namespace
