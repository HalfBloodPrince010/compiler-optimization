#pragma once // NOLINT(llvm-header-guard)

/**
 * @file Earliest Placement
 */
#include "2-WBAvailExpr.h"
#define DEBUG_EPLACE
class EPlaceWrapperPass;

using EPlaceFrameworkBase =
    Framework<Expression, bool, Direction::kForward, Intersect>;
class EPlaceImpl : public EPlaceFrameworkBase {

private:
  // Anticipated Expression OUT
  std::unordered_map<const Instruction *, DomainVal_t> AntiExprInstDomainValMap;

  // Will-be Availble Expression IN
  std::unordered_map<const BasicBlock *, std::vector<bool>>
      WBAvailExprBoundaryVals;

private:
  EPlaceImpl() = default;

  void initialize(std::vector<Expression> AntiExprDomain,
                  std::unordered_map<const Instruction *, DomainVal_t>
                      AntiExprInstDomainValMap,
                  std::unordered_map<const BasicBlock *, std::vector<bool>>
                      WBAvailExprBoundaryVals) {

    std::copy(AntiExprDomain.begin(), AntiExprDomain.end(),
              std::back_inserter(this->Domain));
    this->AntiExprInstDomainValMap = AntiExprInstDomainValMap;
    this->WBAvailExprBoundaryVals = WBAvailExprBoundaryVals;
  }

  virtual void initializeDomainFromInst(const Instruction &Inst) override {
    /*
     * Domain same as Anti-Expression/Will-Be Available Expression, which is set
     * in the initialize method. Just adding debug information to validate the
     * Domain Variables.
     */
#ifdef DEBUG_EPLACE
    if (const BinaryOperator *const BinaryOp =
            dyn_cast<BinaryOperator>(&Inst)) {
      Expression Expr = Expression(*BinaryOp);
      auto it = std::find(Domain.begin(), Domain.end(), Expr);

      if (it != Domain.end()) {
        errs() << "Domain Inst:" << Inst << "\n";
        errs() << "\t\tExpression:"
               << "\n";
        errs() << "\t\t  Opcode:" << Expr.Opcode << "\n";
        errs() << "\t\t  LHS:" << *Expr.LHS << "\n";
        errs() << "\t\t  RHS:" << *Expr.RHS << "\n";
      }
    }
#endif
  }

  virtual bool transferFunc(const Instruction &Inst, const DomainVal_t &IV,
                            DomainVal_t &OV) override {
    return false;
  }

private:
  friend class EPlaceWrapperPass;
};

class EPlaceWrapperPass : public FunctionPass {
private:
  EPlaceImpl EPlace;

public:
  static char ID;

  EPlaceWrapperPass() : FunctionPass(ID) {}

  virtual void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<AntiExprWrapperPass>();
    AU.addRequired<WBAvailExprWrapperPass>();
    AU.setPreservesAll();
  }

  bool runOnFunction(Function &F) override {
    errs() << "* Earliest Placement *"
           << "\n";

    AntiExprWrapperPass &AntiExpr = getAnalysis<AntiExprWrapperPass>();
    WBAvailExprWrapperPass &WBAvailExpr = getAnalysis<WBAvailExprWrapperPass>();

    std::vector<Expression> Domain = AntiExpr.getDomain();
    auto AntiExprInstDomainValMap = AntiExpr.getInstDomainValMap();
    auto WBAvailExprInstDomainValMap = WBAvailExpr.getInstDomainValMap();
    auto WBAvailExprBoundaryVals = WBAvailExpr.getBoundaryVals();

    EPlace.initialize(Domain, AntiExprInstDomainValMap,
                      WBAvailExprBoundaryVals);

    return EPlace.runOnFunction(F);
  }
};
