
#ifndef GO_SYSTEM_H
#define GO_SYSTEM_H

#include <algorithm>
#include <string>
#include <list>
#include <map>
#include <set>
#include <vector>
#include <sstream>
#include <climits>
#include <ctype.h>
#include <stdarg.h>

# include <unordered_map>
# include <unordered_set>

# define Unordered_map(KEYTYPE, VALTYPE) \
	std::unordered_map<KEYTYPE, VALTYPE>

# define Unordered_map_hash(KEYTYPE, VALTYPE, HASHFN, EQFN) \
	std::unordered_map<KEYTYPE, VALTYPE, HASHFN, EQFN>

# define Unordered_set(KEYTYPE) \
	std::unordered_set<KEYTYPE>

# define Unordered_set_hash(KEYTYPE, HASHFN, EQFN) \
	std::unordered_set<KEYTYPE, HASHFN, EQFN>

#include <iostream>
#include <assert.h>
#include <string.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

#define go_assert assert

#define _(x) x

#include "llvm-includes.h"

#define go_unreachable() llvm_unreachable("unreachable")

#define ARRAY_SIZE(A) (sizeof (A) / sizeof ((A)[0]))

#define IS_ABSOLUTE_PATH(x) \
  llvm::sys::path::is_absolute(llvm::Twine(x))

#define HOST_BITS_PER_WIDE_INT 64
#define HOST_WIDE_INT long

#define ISALNUM(x) isalnum(x)

extern const char *lbasename(const char *);
extern const char *xstrerror(int);
extern bool IS_DIR_SEPARATOR(char);
extern bool ISXDIGIT(char);

#define MAX(X,Y) ((X) > (Y) ? (X) : (Y))

#endif // !defined(GO_SYSTEM_H)
