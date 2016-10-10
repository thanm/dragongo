//===-- go-llvm-linemap.h - Linemap class public interfaces  --------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Public interfaces for LLVM implementation of Linemap class.
//
//===----------------------------------------------------------------------===//

#ifndef GO_LLVM_LINEMAP_H
#define GO_LLVM_LINEMAP_H

class Linemap;

extern Linemap *go_get_linemap();

#endif // !defined(GO_LLVM_LINEMAP_H)
