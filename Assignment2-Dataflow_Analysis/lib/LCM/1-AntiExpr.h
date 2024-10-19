#pragma once // NOLINT(llvm-header-guard)

/**
 * @file Anticipated Expression Dataflow Analysis
 */
#include <llvm-12/llvm/IR/InstrTypes.h>
#include <llvm-12/llvm/IR/Instruction.h>
#include <llvm-12/llvm/Support/Casting.h>
#include <llvm/Pass.h>
#include <llvm/Transforms/Utils.h>

#include <dfa/Framework.h>
#include <dfa/MeetOp.h>

#include "../Expression.h"

#define DEBUG_ANTI_EXPR

using namespace dfa;

class AntiExprWrapperPass;

using AntiExprFrameworkBase =
    Framework<Expression, bool, Direction::kBackward, Intersect>;

class AntiExprImpl : public AntiExprFrameworkBase {
private:
  virtual void initializeDomainFromInst(const Instruction &Inst) override {
    if (const BinaryOperator *const BinaryOp =
            dyn_cast<BinaryOperator>(&Inst)) {
      Expression Expr = Expression(*BinaryOp);

#ifdef DEBUG_ANTI_EXPR
      errs() << "Domain Inst:" << Inst << "\n";
      errs() << "\t\tExpression:"
             << "\n";
      errs() << "\t\t  Opcode:" << Expr.Opcode << "\n";
      errs() << "\t\t  LHS:" << *Expr.LHS << "\n";
      errs() << "\t\t  RHS:" << *Expr.RHS << "\n";
#endif // DEBUG

      if (std::find(Domain.begin(), Domain.end(), Expr) == Domain.end()) {
        Domain.emplace_back(Expr);
      }
    }
  }
  virtual bool transferFunc(const Instruction &Inst, const DomainVal_t &IV,
                            DomainVal_t &OV) override {

    int Idx = 0;
    DomainVal_t TEMP_OV = IV;
    /*
     * Step 1: Generate the Anticipated Expression.
     *
     */
    if (const BinaryOperator *const BinaryOp =
            dyn_cast<BinaryOperator>(&Inst)) {
      Expression Expr = Expression(*BinaryOp);
      auto it = std::find(Domain.begin(), Domain.end(), Expr);

      if (it != Domain.end()) {
        int index = it - Domain.begin();
        TEMP_OV.at(index) = true;
      }
    }

    /*
     * Step 2: Kill the expression who LHS or RHS equal Inst,
     * because its redefined
     *
     */
    for (Expression Expr : Domain) {
      bool redefined = false;
      if (auto *InstLHS = dyn_cast<Instruction>(Expr.LHS)) {
        if (InstLHS == &Inst) {
          redefined = true;
        }
      }

      if (auto *InstRHS = dyn_cast<Instruction>(Expr.RHS)) {
        if (InstRHS == &Inst) {
          redefined = true;
        }
      }

      if (redefined) {
        TEMP_OV.at(Idx) = false;
      }

      Idx++;
    }

    bool isChanged = TEMP_OV != OV;

    OV = TEMP_OV;

    return isChanged;
  }

private:
  friend class AntiExprWrapperPass;
};

class AntiExprWrapperPass : public FunctionPass {
private:
  AntiExprImpl AntiExpr;

public:
  static char ID;

  AntiExprWrapperPass() : FunctionPass(ID) {}

  virtual void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequiredID(BreakCriticalEdgesID);
    AU.setPreservesAll();
  }

  bool runOnFunction(Function &F) override {
    errs() << "* Anticipated Expression *"
           << "\n";

    return AntiExpr.runOnFunction(F);
  }

  std::vector<Expression> getDomain() const { return AntiExpr.Domain; }
  std::unordered_map<const Instruction *, std::vector<bool>>
  getInstDomainValMap() const {
    return AntiExpr.InstDomainValMap;
  }
};
