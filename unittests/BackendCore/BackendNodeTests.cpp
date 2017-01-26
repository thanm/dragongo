//===- llvm/tools/dragongo/unittests/BackendCore/BackendNodeTests.cpp -----===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "bnode.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "operator.h"
#include "gtest/gtest.h"
#include "DiffUtils.h"

using namespace goBackendUnitTests;

namespace {

class SimpleVisitor {
 public:
  SimpleVisitor(unsigned stopAt = 0xffffffff) : stopAt_(stopAt) { }

  std::pair<VisitDisp, Bnode *> visitNodePre(Bnode *node) {
    ss_ << "node " << node->flavstr() << " pre " << node->id() << "\n";
    VisitDisp disp = (stopAt_ == node->id() ? StopWalk : ContinueWalk);
    return std::make_pair(disp, node);
  }
  std::pair<VisitDisp, Bnode *> visitNodePost(Bnode *node) {
    ss_ << "node " << node->flavstr() << " post " << node->id() << "\n";
    VisitDisp disp = (stopAt_ == node->id() ? StopWalk : ContinueWalk);
    return std::make_pair(disp, node);
  }

  std::pair<VisitDisp, Bnode *> visitChildPre(Bnode *parent, Bnode *child) {
    ss_ << "child pre " << parent->id() << " " << child->id() << "\n";
    VisitDisp disp = (stopAt_ == child->id() ? StopWalk : ContinueWalk);
    return std::make_pair(disp, child);
  }
  std::pair<VisitDisp, Bnode *> visitChildPost(Bnode *parent, Bnode *child) {
    ss_ << "child post " << parent->id() << " " << child->id() << "\n";
    VisitDisp disp = (stopAt_ == child->id() ? StopWalk : ContinueWalk);
    return std::make_pair(disp, child);
  }

  std::string str() const { return ss_.str(); }

 private:
  std::stringstream ss_;
  unsigned stopAt_;
};

TEST(BackendNodeTests, MakeBoolConstExpr) {

  BnodeBuilder builder;
  Bexpression *cv = builder.mkConst(nullptr, nullptr);
  Bexpression *ve = builder.mkVar(nullptr);
  Bexpression *add = builder.mkBinaryOp(OPERATOR_PLUS, cv, ve, Location());
  Bexpression *ve2 = builder.mkVar(nullptr);
  Bexpression *der = builder.mkDeref(ve2, Location());
  Bexpression *sub = builder.mkBinaryOp(OPERATOR_MINUS, add, der, Location());

  SimpleVisitor vis;
  Bnode *res1 = walk_nodes(sub, vis);

  EXPECT_EQ(res1, sub);

  const char *exp = R"RAW_RESULT(
      node binary pre 6
      child pre 6 3
      node binary pre 3
      child pre 3 1
      node const pre 1
      node const post 1
      child post 3 1
      child pre 3 2
      node var pre 2
      node var post 2
      child post 3 2
      node binary post 3
      child post 6 3
      child pre 6 5
      node deref pre 5
      child pre 5 4
      node var pre 4
      node var post 4
      child post 5 4
      node deref post 5
      child post 6 5
      node binary post 6
    )RAW_RESULT";

  std::string reason;
  bool equal = difftokens(exp, vis.str(), reason);
  EXPECT_EQ("pass", equal ? "pass" : reason);
  if (!equal) {
    std::cerr << "expected dump:\n" << exp << "\n";
    std::cerr << "result dump:\n" << vis.str() << "\n";
  }
}

}
