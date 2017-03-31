//===-- go-llvm-irbuilders.cpp - 'BlockLIRBuilder' class methods ----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Methods for class BlockLIRBuilder.
//
//===----------------------------------------------------------------------===//

#include "go-llvm-irbuilders.h"
#include "namegen.h"

std::vector<llvm::Instruction*> BlockLIRBuilder::instructions()
{
  std::vector<llvm::Instruction*> rv;
  for (auto &i : dummyBlock_->getInstList())
    rv.push_back(&i);
  for (auto &i : rv) {
    i->removeFromParent();
    // hack: irbuilder likes to create unnamed bitcasts
    if (i->isCast() && i->getName() == "")
      i->setName(namegen_->namegen("cast"));
  }
  return rv;
}
