// SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
// SPDX-License-Identifier: GPL-3.0-only
// cmakedefine

// undefined
/* #undef noval */

// 1
#define trueval 1

// undefined
/* #undef falseval */

// undefined
/* #undef zeroval */

// 1
#define oneval 1

// 1
#define tenval 1

// 1
#define stringval 1


// cmakedefine01

// 0
#define boolnoval 0

// 1
#define booltrueval 1

// 0
#define boolfalseval 0

// 0
#define boolzeroval 0

// 1
#define booloneval 1

// 1
#define booltenval 1

// 1
#define boolstringval 1

// @ substition

// undefined variable, removed
// 

// no value, removed
// 

// 1
// 1

// 0
// 0

// 0
// 0

// 1
// 1

// 10
// 10

// test
// test


// curly brackets substition

// empty curly brackets, removed
// this does not work with zig 0.15.2
// $ {}

// undefined variable, removed
// 

// no value, removed
// 

// 1
// 1

// 0
// 0

// 0
// 0

// 1
// 1

// 10
// 10

// test
// test


// sigil test (no changes)
#define AT @
#define ATAT @@
#define ATATAT @@@
#define ATATATAT @@@@
