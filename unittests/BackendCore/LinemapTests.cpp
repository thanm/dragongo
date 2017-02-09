//===- llvm/tools/dragongo/unittests/BackendCore/LinemapTests.cpp -----===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "TestUtils.h"
#include "gtest/gtest.h"
#include "go-llvm-linemap.h"
#include "go-location.h"

namespace {

TEST(LinemapTests, CreateLinemap) {
  std::unique_ptr<Linemap> lm(go_get_linemap());
}

TEST(LinemapTests, BasicLinemap) {
  std::unique_ptr<Llvm_linemap> lm(new Llvm_linemap());

  Location ul = Linemap::unknown_location();
  Location pdl = Linemap::predeclared_location();
  EXPECT_TRUE(ul.handle() != pdl.handle());
  EXPECT_EQ(lm->to_string(ul), "");
  EXPECT_EQ(lm->to_string(pdl), "");
  EXPECT_EQ(lm->get_initial_file(), "");

  lm->start_file("foo.go", 10);
  EXPECT_EQ(lm->to_string(ul), "");
  EXPECT_EQ(lm->to_string(pdl), "");
  EXPECT_EQ(lm->get_initial_file(), "foo.go");
  Location f10 = lm->get_location(1);
  lm->start_line(12, 256);
  Location f12 = lm->get_location(1);
  Location f12c5 = lm->get_location(5);
  Location f12x = lm->get_location(1);
  EXPECT_TRUE(f10.handle() != ul.handle());
  EXPECT_TRUE(f10.handle() != pdl.handle());
  EXPECT_TRUE(f12.handle() != f10.handle());
  EXPECT_TRUE(f12.handle() != f12c5.handle());
  EXPECT_NE(f12x.handle(), f12.handle()); // no line/col hashing currently
  EXPECT_EQ(lm->to_string(f12x), lm->to_string(f12));
  EXPECT_EQ(lm->location_line(f10), 10);
  EXPECT_EQ(lm->location_file(f10), std::string("foo.go"));
  EXPECT_EQ(lm->location_line(f12), 12);
  EXPECT_EQ(lm->location_column(f12), 1u);
  EXPECT_EQ(lm->location_column(f12c5), 5u);
  EXPECT_EQ(lm->location_line(f12x), 12);
  EXPECT_EQ(lm->location_line(f12c5), 12);

  lm->start_file("blix.go", 89);
  EXPECT_EQ(lm->get_initial_file(), "foo.go");
  lm->start_file("/tmp/bar.go", 1);
  Location b1 = lm->get_location(1);
  lm->start_line(22, 0);
  Location b22 = lm->get_location(1);
  Location b22c9 = lm->get_location(9);
  EXPECT_TRUE(b22.handle() != b22c9.handle());
  std::string b22s = lm->to_string(b22);
  std::string b22c9s = lm->to_string(b22c9);
  EXPECT_EQ(b22s, b22c9s);
  EXPECT_EQ(b22s, "bar.go:22");
  lm->start_file("foo.go", 10);
  Location x10 = lm->get_location(1);
  EXPECT_EQ(lm->to_string(b22), b22s);
  EXPECT_TRUE(x10.handle() != b1.handle());
  lm->start_line(12, 256);
  Location x12 = lm->get_location(1);
  EXPECT_NE(x12.handle(), f12.handle());
  EXPECT_EQ(lm->to_string(x12), lm->to_string(f12));

  std::string stats = lm->statistics();
  EXPECT_EQ(stats, "accesses=9 files=4 segments=3 "
            "locmem=22 bytes/location=2.4");
}

}
