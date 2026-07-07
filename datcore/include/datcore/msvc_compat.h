#pragma once
// Minimal shims so MSVC-authored reference code (GWMB) compiles under clang/gcc.
#include <cstdint>
#if !defined(_MSC_VER)
using __int64 = long long;
using __int32 = int;
using __int16 = short;
#endif
