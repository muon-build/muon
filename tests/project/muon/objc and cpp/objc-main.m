// SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
// SPDX-License-Identifier: GPL-3.0-only

#include <assert.h>
#include <stdio.h>
#import "calc.h"

int main() {
    @autoreleasepool {
        Calc* calc = [[Calc alloc] init];
        int result = [calc add:40 to:2];
        printf("Objective-C result is %d\n", result);
        // [calc release];
        assert(result == 42);
        return 42 - result;
    }
}
