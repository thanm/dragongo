//===-- go-llvm-bstatement.cpp - implementation of 'Bstatement' class ---===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Methods for class Bstatement.
//
//===----------------------------------------------------------------------===//

#include "go-llvm-btype.h"
#include "go-llvm-bstatement.h"
#include "go-system.h"

#include "llvm/Support/raw_ostream.h"

static void indent(llvm::raw_ostream &os, unsigned ilevel) {
  for (unsigned i = 0; i < ilevel; ++i)
    os << " ";
}

void Bstatement::dump() {
  std::string s;
  llvm::raw_string_ostream os(s);
  osdump(os, 0, false);
  std::cerr << os.str();
}

void Bstatement::osdump(llvm::raw_ostream &os, unsigned ilevel, bool terse)
{
  switch (flavor()) {
  case ST_Compound: {
    CompoundStatement *cst = castToCompoundStatement();
    if (! terse) {
      indent(os, ilevel);
      os << ((void*)this) << " {\n";
    }
    for (auto st : cst->stlist())
      st->osdump(os, ilevel + 2, terse);
    if (! terse) {
      indent(os, ilevel);
      os << "}\n";
    }
    break;
  }
  case ST_ExprList: {
    ExprListStatement *elst = castToExprListStatement();
    if (! terse) {
      indent(os, ilevel);
      os << ((void*)this) << " [\n";
    }
    for (auto expr : elst->expressions())
      expr->osdump(os, ilevel + 2, terse);
    if (! terse) {
      indent(os, ilevel);
      os << "]\n";
    }
    break;
  }
  case ST_IfPlaceholder: {
    IfPHStatement *ifst = castToIfPHStatement();
    indent(os, ilevel);
    os << "if";
    if (! terse)
      os << " " << ((void*) ifst);
    os << ":\n";
    indent(os, ilevel + 2);
    os << "cond:\n";
    ifst->cond()->osdump(os, ilevel + 2, terse);
    if (ifst->trueStmt()) {
      indent(os, ilevel + 2);
      os << "then:\n";
      ifst->trueStmt()->osdump(os, ilevel + 2, terse);
    }
    if (ifst->falseStmt()) {
      indent(os, ilevel + 2);
      os << "else:\n";
      ifst->falseStmt()->osdump(os, ilevel + 2, terse);
    }
    break;
  }
  case ST_Goto: {
    GotoStatement *gst = castToGotoStatement();
    indent(os, ilevel);
    os << "goto L" << gst->targetLabel() << "\n";
    break;
  }
  case ST_Label: {
    LabelStatement *lbst = castToLabelStatement();
    indent(os, ilevel);
    os << "label L" << lbst->definedLabel() << "\n";
    break;
  }

  case ST_SwitchPlaceholder:
    os << "not yet implemented\n";
    break;
  }
}

void Bstatement::destroy(Bstatement *stmt, WhichDel which) {
  assert(stmt);
  switch (stmt->flavor()) {
  case ST_Compound: {
    CompoundStatement *cst = stmt->castToCompoundStatement();
    for (auto st : cst->stlist())
      destroy(st, which);
    break;
  }
  case ST_ExprList: {
    ExprListStatement *elst = stmt->castToExprListStatement();
    for (auto expr : elst->expressions())
      Bexpression::destroy(expr, which);
    break;
  }
  case ST_IfPlaceholder: {
    IfPHStatement *ifst = stmt->castToIfPHStatement();
    Bexpression::destroy(ifst->cond(), which);
    if (ifst->trueStmt())
      Bstatement::destroy(ifst->trueStmt(), which);
    if (ifst->falseStmt())
      Bstatement::destroy(ifst->falseStmt(), which);
    break;
  }
  case ST_Goto:
  case ST_Label: {
    // nothing to do here at the moment
    break;
  }

  case ST_SwitchPlaceholder:
    assert(false && "not yet implemented");
    break;
  }
  if (which != DelInstructions)
    delete stmt;
}
