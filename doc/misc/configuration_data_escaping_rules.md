<!--
SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
SPDX-License-Identifier: GPL-3.0-only
-->

Configuration data escaping rules:

1. You can escape the format character (@) by preceding it with a backslash.

2. Backslashes not directly preceding a format character are not modified.

3. The number of backslashes preceding a @ in the output is equal to the number
   of backslashes in the input divided by two, rounding down.

   For example, given the string "\\@" (one backslash), the output will contain
   no backslashes.  Both "\\\\@" and "\\\\\\@" (two and three backslashes) will
   produce one backslash in the output "\\@".

4. If the configuration format is cmake and the number of backslashes is even,
   don't escape the variable.  Otherwise, always escape the variable.

Examples:

"\\@" -> "@"

"\\\\@" -> "\\@"

"\\\\ @" -> "\\\\ @"
