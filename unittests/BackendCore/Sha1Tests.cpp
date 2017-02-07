//===- llvm/tools/dragongo/unittests/BackendCore/Sha1Tests.cpp ------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "TestUtils.h"
#include "gtest/gtest.h"
#include "llvm/ADT/StringExtras.h"
#include "go-sha1.h"

namespace {

TEST(Sha1Tests, CreateSha1Helper) {
  std::unique_ptr<Go_sha1_helper> sh1(go_create_sha1_helper());
}

TEST(Sha1Tests, BasicSha1Test1) {
  std::unique_ptr<Go_sha1_helper> sh1(go_create_sha1_helper());
  const char *grist = "hi mom\n";
  sh1->process_bytes(grist, strlen(grist));
  std::string raw = sh1->finish();
  std::string hexed = llvm::toHex(raw);
  EXPECT_EQ(hexed, "9F74809A2EE7607B16FCC70D9399A4DE9725A727");
  EXPECT_EQ(hexed.size(), 40);

}

TEST(Sha1Tests, BasicSha1Test2) {
  std::unique_ptr<Go_sha1_helper> sh1(go_create_sha1_helper());
  const char *grist = "Now is the winter of our discontent.";
  sh1->process_bytes(grist, strlen(grist));
  std::string raw = sh1->finish();
  std::string hexed = llvm::toHex(raw);
  EXPECT_EQ(hexed, "41A42F61DB4BA8089B20E45CFB685FADEE00F4D4");
  EXPECT_EQ(hexed.size(), 40);
}

}
