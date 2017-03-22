//===- llvm/tools/dragongo/unittests/BackendCore/BackendNodeTests.cpp -----===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "go-llvm-bnode.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "operator.h"
#include "gtest/gtest.h"
#include "TestUtils.h"
#include "DiffUtils.h"

using namespace goBackendUnitTests;

namespace {

class SimpleVisitor {
 public:
  SimpleVisitor(unsigned stopAt = 0xffffffff) : stopAt_(stopAt) { }

  std::pair<VisitDisp, Bnode *> visitNodePre(Bnode *node) {
    ss_ << "node " << node->flavstr() << " pre " << id(node) << "\n";
    VisitDisp disp = (stopAt_ == id(node) ? StopWalk : ContinueWalk);
    return std::make_pair(disp, node);
  }
  std::pair<VisitDisp, Bnode *> visitNodePost(Bnode *node) {
    ss_ << "node " << node->flavstr() << " post " << id(node) << "\n";
    VisitDisp disp = (stopAt_ == id(node) ? StopWalk : ContinueWalk);
    return std::make_pair(disp, node);
  }

  std::pair<VisitDisp, Bnode *> visitChildPre(Bnode *parent, Bnode *child) {
    ss_ << "pre child " << child->flavstr() << " " << id(parent) << " " << id(child) << "\n";
    VisitDisp disp = (stopAt_ == id(child) ? StopWalk : ContinueWalk);
    return std::make_pair(disp, child);
  }
  std::pair<VisitDisp, Bnode *> visitChildPost(Bnode *parent, Bnode *child) {
    ss_ << "post child " << child->flavstr() << " " << id(parent) << " " << id(child) << "\n";
    VisitDisp disp = (stopAt_ == id(child) ? StopWalk : ContinueWalk);
    return std::make_pair(disp, child);
  }

  std::string str() const { return ss_.str(); }

  void setIds(const std::vector<Bnode *> &nodes) {
    for (auto &n : nodes) {
      assert(ids_.find(n) == ids_.end());
      unsigned id = ids_.size();
      ids_[n] = id;
    }
  }
  unsigned id(Bnode *node) {
    return (ids_.find(node) != ids_.end() ?
            ids_[node]+1 : 0);
  }

 private:
  std::stringstream ss_;
  std::map<Bnode *, unsigned> ids_;
  unsigned stopAt_;
};

TEST(BackendNodeTests, VerifyVisitorBehavior) {

  FcnTestHarness h("foo");
  Llvm_backend *be = h.be();
  Location loc;

  SimpleVisitor vis;

  Btype *bi32t = be->integer_type(false, 32);
  Btype *bpi32t = be->pointer_type(bi32t);
  Bexpression *c22 = mkInt32Const(be, 22);
  Bvariable *xv = h.mkLocal("x", bi32t);
  Bvariable *yv = h.mkLocal("y", bpi32t);
  Bexpression *ve = be->var_expression(xv, VE_rvalue, loc);
  Bexpression *add = be->binary_expression(OPERATOR_PLUS, c22, ve, loc);
  Bexpression *ve2 = be->var_expression(yv, VE_rvalue, loc);
  Bexpression *der = be->indirect_expression(bi32t, ve2, false, loc);
  Bexpression *sub = be->binary_expression(OPERATOR_MINUS, add, der, loc);

  std::vector<Bnode *> nodes = { c22, ve, add, ve2, der, sub };
  vis.setIds(nodes);

  Bnode *res1 = update_walk_nodes(sub, vis);

  EXPECT_EQ(res1, sub);

  const char *exp = R"RAW_RESULT(
     node - pre 6
     pre child + 6 3
     node + pre 3
     pre child const 3 1
     node const pre 1
     node const post 1
     post child const 3 1
     pre child deref 3 0
     node deref pre 0
     pre child var 0 2
     node var pre 2
     node var post 2
     post child var 0 2
     node deref post 0
     post child deref 3 0
     node + post 3
     post child + 6 3
     pre child deref 6 0
     node deref pre 0
     pre child deref 0 5
     node deref pre 5
     pre child var 5 4
     node var pre 4
     node var post 4
     post child var 5 4
     node deref post 5
     post child deref 0 5
     node deref post 0
     post child deref 6 0
     node - post 6
    )RAW_RESULT";

  std::string reason;
  bool equal = difftokens(exp, vis.str(), reason);
  EXPECT_EQ("pass", equal ? "pass" : reason);
  if (!equal) {
    std::cerr << "expected dump:\n" << exp << "\n";
    std::cerr << "result dump:\n" << vis.str() << "\n";
  }

  h.mkExprStmt(sub);

  bool broken = h.finish(PreserveDebugInfo);
  EXPECT_FALSE(broken && "Module failed to verify.");
}

TEST(BackendNodeTests, CloneSubtree) {

  FcnTestHarness h("foo");
  Llvm_backend *be = h.be();
  Location loc = h.loc();

  // (x - int32(z)) + *y
  Btype *bi16t = be->integer_type(false, 16);
  Btype *bi32t = be->integer_type(false, 32);
  Btype *bpi32t = be->pointer_type(bi32t);
  Bvariable *xv = h.mkLocal("x", bi32t);
  Bvariable *yv = h.mkLocal("y", bpi32t);
  Bvariable *zv = h.mkLocal("z", bi16t);
  Bexpression *vex = be->var_expression(xv, VE_rvalue, loc);
  Bexpression *vez = be->var_expression(zv, VE_rvalue, loc);
  Bexpression *conv = be->convert_expression(bi32t, vez, loc);
  Bexpression *sub = be->binary_expression(OPERATOR_PLUS, vex, conv, loc);
  Bexpression *vey = be->var_expression(yv, VE_rvalue, loc);
  Bexpression *der = be->indirect_expression(bi32t, vey, false, loc);
  Bexpression *add = be->binary_expression(OPERATOR_PLUS, sub, der, loc);

  Bexpression *addclone = be->nodeBuilder().cloneSubtree(add);
  EXPECT_NE(add, addclone);

  const char *exp = R"RAW_RESULT(
  %x.ld.0 = load i32, i32* %x
  %cast.0 = bitcast i16* %z to i32*
  %.ld.0 = load i32, i32* %cast.0
  %add.0 = add i32 %x.ld.0, %.ld.0
  %y.ld.0 = load i32*, i32** %y
  %.ld.1 = load i32, i32* %y.ld.0
  %add.1 = add i32 %add.0, %.ld.1
    )RAW_RESULT";

  bool isOK = h.expectRepr(add, exp);
  EXPECT_TRUE(isOK && "expr does not have expected contents");
  isOK = h.expectRepr(addclone, exp);
  EXPECT_TRUE(isOK && "cloned expr does not have expected contents");

  h.mkExprStmt(add);
  h.mkExprStmt(addclone);

  bool broken = h.finish(PreserveDebugInfo);
  EXPECT_FALSE(broken && "Module failed to verify.");

}

TEST(BackendNodeTests, FixSharing) {

  FcnTestHarness h("foo");
  Llvm_backend *be = h.be();
  Location loc = h.loc();

  Btype *bi32t = be->integer_type(false, 32);
  Btype *bpi32t = be->pointer_type(bi32t);
  Btype *s2t = mkBackendStruct(be, bpi32t, "f1", bi32t, "f2", nullptr);
  Btype *s4t = mkBackendStruct(be, s2t, "f1", s2t, "f2", nullptr);
  Bvariable *xv = h.mkLocal("x", s4t);

  // y = x.f1.f1 + x.f1.f1 [with sharing]
  Bexpression *vex = be->var_expression(xv, VE_rvalue, loc);
  Bexpression *f1ex = be->struct_field_expression(vex, 0, loc);
  Bexpression *f2ex = be->struct_field_expression(f1ex, 0, loc);
  Bexpression *der = be->indirect_expression(bi32t, f2ex, false, loc);
  Bexpression *add = be->binary_expression(OPERATOR_PLUS, der, der, loc);

  const char *exp2 = R"RAW_RESULT(
  %field.0 = getelementptr inbounds { { i32*, i32 }, { i32*, i32 } }, { { i32*, i32 }, { i32*, i32 } }* %x, i32 0, i32 0
  %field.1 = getelementptr inbounds { i32*, i32 }, { i32*, i32 }* %field.0, i32 0, i32 0
  %x.field.field.ld.0 = load i32*, i32** %field.1
  %.ld.0 = load i32, i32* %x.field.field.ld.0
  %field.0 = getelementptr inbounds { { i32*, i32 }, { i32*, i32 } }, { { i32*, i32 }, { i32*, i32 } }* %x, i32 0, i32 0
  %field.1 = getelementptr inbounds { i32*, i32 }, { i32*, i32 }* %field.0, i32 0, i32 0
  %x.field.field.ld.0 = load i32*, i32** %field.1
  %.ld.1 = load i32, i32* %x.field.field.ld.0
  %add.0 = add i32 %.ld.0, %.ld.1
    )RAW_RESULT";

  bool isOK = h.expectRepr(add, exp2);
  EXPECT_TRUE(isOK && "expr does not have expected contents");

  h.mkExprStmt(add);

  bool broken = h.finish(PreserveDebugInfo);
  EXPECT_FALSE(broken && "Module failed to verify.");
}

}
