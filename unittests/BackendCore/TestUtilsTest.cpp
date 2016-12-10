//===- llvm/tools/dragongo/unittests/BackendCore/TestUtilstest.cpp -===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "TestUtils.h"
#include "gtest/gtest.h"

using namespace llvm;
using namespace goBackendUnitTests;

namespace {

TEST(TestUtilsTests, ContainsTokens) {
  EXPECT_TRUE(containstokens("a b c   d", "b c"));
  EXPECT_TRUE(containstokens("a b c   d", ""));
  EXPECT_FALSE(containstokens("a b c   d", "b d"));
  EXPECT_FALSE(containstokens("a b c   d", "a b c d e f"));
}

TEST(TestUtilsTests, DiffTokens) {
  std::string reason;
  EXPECT_TRUE(difftokens("a b c   d", "   a        b c d   ", reason));
  EXPECT_FALSE(difftokens("a b c", "a b c d", reason));
  EXPECT_TRUE(containstokens(reason, "lengths differ (3 vs 4) extra expected"));
  EXPECT_FALSE(difftokens("a b c", "a b", reason));
  EXPECT_TRUE(containstokens(reason, "lengths differ (3 vs 2) extra result"));
  EXPECT_FALSE(difftokens("a b c", "a d c", reason));
  EXPECT_TRUE(containstokens(reason, "token vector diff at slot 1"));
  EXPECT_FALSE(difftokens("x b c", "a b c", reason));
  EXPECT_TRUE(containstokens(reason, "token vector diff at slot 0"));
  EXPECT_FALSE(difftokens("a b c", "a b x", reason));
  EXPECT_TRUE(containstokens(reason, "token vector diff at slot 2"));
}

}
