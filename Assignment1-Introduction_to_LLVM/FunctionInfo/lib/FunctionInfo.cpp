#include <iterator>
#include <llvm/IR/Module.h>
#include <llvm/Pass.h>
#include <llvm/Support/raw_ostream.h>

using namespace llvm;

namespace {

class FunctionInfo final : public ModulePass {
public:
  static char ID;

  FunctionInfo() : ModulePass(ID) {}

  // We don't modify the program, so we preserve all analysis.
  virtual void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesAll();
  }

  virtual bool runOnModule(Module &M) override {
    for (auto &F : M) {
      outs() << "Function Name: " << F.getName() << "\n";
      outs() << "Number of Arguments: " << F.arg_size() << "\n";
      outs() << "Number of Calls: " << F.getNumUses() << "\n";
      /* int basicBlockCount = 0;
      int instrCount = 0;
      for (auto &B : F) {
        basicBlockCount += 1;
        for (auto &I : B) {
          (void)I;
          instrCount += 1;
        }
      }
      errs() << "Number of Basic Blocks: " << basicBlockCount << "\n";
      errs() << "Number of Instructions: " << instrCount << "\n";
      */
      outs() << "Number OF BBs: " << std::distance(F.begin(), F.end())
             << "\n";
      outs() << "Number of Instructions: " << F.getInstructionCount() << "\n";
    }

    return false;
  }
}; // class FunctionInfo

char FunctionInfo::ID = 0;
RegisterPass<FunctionInfo> X("function-info", "CSCD70: Function Information");

} // anonymous namespace
