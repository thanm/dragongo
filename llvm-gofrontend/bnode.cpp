//===-- bnode.cpp - implementation of 'Bnode' class ---=======================//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Methods for class Bnode and related classes.
//
//===----------------------------------------------------------------------===//

#include "go-llvm-btype.h"
#include "go-system.h"

#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Value.h"

#include "bnode.h"

// Table of Bnode properties

enum StmtDisp : unsigned char { IsStmt, IsExpr };

struct BnodePropVals {
  const char *str;
  unsigned numChildren;
  StmtDisp stmt;
};

static constexpr unsigned Variadic = 0xffffffff;

static BnodePropVals BnodeProperties[] = {

  /* N_Error */       {  "error", 0, IsExpr },
  /* N_Const */       {  "const", 0, IsExpr },
  /* N_Var */         {  "var", 0, IsExpr },
  /* N_Conversion */  {  "conv", 1, IsExpr },
  /* N_Deref */       {  "deref", 1, IsExpr },
  /* N_Address */     {  "addr", 1, IsExpr },
  /* N_UnaryOp */     {  "unary", 1, IsExpr },
  /* N_StructField */ {  "field", 1, IsExpr },
  /* N_BinaryOp */    {  "binary", 2, IsExpr },
  /* N_Compound */    {  "compound", 2, IsExpr },
  /* N_ArrayIndex */  {  "arindex", 2, IsExpr },
  /* N_Composite */   {  "composite", Variadic, IsExpr },

  /* N_EmptyStmt */   {  "empty", 0, IsStmt },
  /* N_LabelStmt */   {  "label", 0, IsStmt },
  /* N_GotoStmt */    {  "goto", 0, IsStmt },
  /* N_InitStmt */    {  "init", 2, IsStmt },
  /* N_AssignStmt */  {  "assign", 2, IsStmt },
  /* N_IfStmt */      {  "ifstmt", 3, IsStmt },
  /* N_BlockStmt */   {  "block", Variadic, IsStmt },
  /* N_ReturnStmt */  {  "return", Variadic, IsStmt },
  /* N_SwitchStmt */  {  "switch", Variadic, IsStmt }
};

Bnode::Bnode(NodeFlavor flavor, const std::vector<Bnode *> &kids,
             Btype *btype, Location loc, unsigned id)
    : kids_(kids)
    , btype_(btype)
    , value_(nullptr)
    , location_(loc)
    , flavor_(flavor)
    , id_(id)
    , flags_(0)
{
  memset(&u, '\0', sizeof(u));
  assert(BnodeProperties[flavor].numChildren == kids.size());
  if (BnodeProperties[flavor].stmt == IsStmt)
    flags_ |= STMT;
  // else assert(btype);
}

void Bnode::replaceChild(unsigned idx, Bnode *newchild)
{
  assert(idx < kids_.size());
  assert(kids_[idx]->isStmt() == newchild->isStmt());
  kids_[idx] = newchild;
}

const char *Bnode::flavstr() const {
  return BnodeProperties[flavor()].str;
}

Bexpression *BnodeBuilder::mkConst(Btype *btype, llvm::Value *value)
{
  // assert(btype);
  // assert(value);
  std::vector<Bnode *> kids;
  return new Bexpression(N_Const, kids, btype, Location(), idCounter_++);
}

Bexpression *BnodeBuilder::mkVar(Bvariable *var)
{
  // assert(var);
  //  Btype *vt = var->btype();

  std::vector<Bnode *> kids;
  Btype *vt = nullptr;
  Bexpression *rval =
      new Bexpression(N_Var, kids, vt, Location(), idCounter_++);
  rval->u.var = var;
  return rval;
}

Bexpression *BnodeBuilder::mkBinaryOp(Operator op, Bexpression *left,
                                      Bexpression *right, Location loc)
{
  std::vector<Bnode *> kids = { left, right };
  Btype *vt = nullptr;
  Bexpression *rval =
      new Bexpression(N_BinaryOp, kids, vt, Location(), idCounter_++);
  rval->u.op = op;
  return rval;
}

Bexpression *BnodeBuilder::mkDeref(Bexpression *src, Location loc)
{
  std::vector<Bnode *> kids = { src };
  Btype *vt = nullptr;
  Bexpression *rval =
      new Bexpression(N_Deref, kids, vt, Location(), idCounter_++);
  return rval;
}
