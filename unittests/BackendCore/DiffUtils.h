//===- llvm/tools/dragongo/unittests/BackendCore/DiffUtils.h --------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef DRAGONGO_UNITTESTS_BACKENDCORE_DIFFUTILS_H
#define DRAGONGO_UNITTESTS_BACKENDCORE_DIFFUTILS_H

#include <string>
#include <vector>

#define RAW_RESULT(x) #x

namespace goBackendUnitTests {

// Trim leading and trailing spaces
std::string trimsp(const std::string &s);

// Split specified string into tokens (with whitespace as delimiter)
std::vector<std::string> tokenize(const std::string &s);

// Join together vector of strings to single string, separate with spaces
std::string vectostr(const std::vector<std::string> &tv);

// Tokenize the two strings, then diff the resulting token vectors,
// returning TRUE if they are identical or FALSE if different (and
// setting 'diffreason' to explanation of diff)
bool difftokens(const std::string &expected,
                const std::string &result,
                std::string &diffreason);

// Return TRUE if string 'text' contains instead of string 'pat'.
// Tokenizes both strings to avoid whitespace differences
bool containstokens(const std::string &text, const std::string &pat);

}

#endif // DRAGONGO_UNITTESTS_BACKENDCORE_DIFFUTILS_H
