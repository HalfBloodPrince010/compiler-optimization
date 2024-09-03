#pragma once // NOLINT(llvm-header-guard)

#include <cassert>
#include <vector>

namespace dfa {

template <typename TDomainElemRepr> //
class MeetOp {
protected:
  using DomainVal_t = std::vector<TDomainElemRepr>;

public:
  virtual DomainVal_t operator()(const DomainVal_t &LHS,
                                 const DomainVal_t &RHS) const = 0;
  virtual DomainVal_t top(const size_t DomainSize) const = 0;
};

/**
 * @brief Intersection Meet Operator
 *
 * Logical AND between LHS and RHS
 *
 * "top" value is universal set because for a "initial backedge" if its all
 * false (null), then all merge/intersection with "top" would result in all
 * false/null set.
 */
class Intersect final : public MeetOp<bool> {
public:
  virtual DomainVal_t operator()(const DomainVal_t &LHS,
                                 const DomainVal_t &RHS) const override {

    if (LHS.size() != RHS.size()) {
      assert(false && "Size of domain values for merge has to be the same");
    }

    DomainVal_t result(LHS.size(), false);
    for (int i = 0; i < LHS.size(); i++) {
      result[i] = LHS[i] && RHS[i];
    }

    /*
    result.reserve(LHS.size());
    std::transform(LHS.begin(), LHS.end(), std::back_inserter(result),
                   result.begin(), std::logical_or<>());
    */

    return result;
  }

  virtual DomainVal_t top(const size_t DomainSize) const override {

    return DomainVal_t(DomainSize, true);
  }
};

/**
 * @brief Union Meet Operator
 *
 * Logical OR between LHS and RHS
 *
 * "top" value is null set.
 */
class Union final : public MeetOp<bool> {
public:
  virtual DomainVal_t operator()(const DomainVal_t &LHS,
                                 const DomainVal_t &RHS) const override {
    if (LHS.size() != RHS.size()) {
      assert(false && "Size of domain values for merge has to be the same");
    }

    DomainVal_t result(LHS.size(), false);
    for (int i = 0; i < LHS.size(); i++) {
      result[i] = LHS[i] || RHS[i];
    }

    return result;
  }

  virtual DomainVal_t top(const size_t DomainSize) const override {

    return DomainVal_t(DomainSize, false);
  }
};
} // namespace dfa
