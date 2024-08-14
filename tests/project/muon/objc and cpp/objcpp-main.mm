// SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
// SPDX-License-Identifier: GPL-3.0-only

#include <assert.h>
#include <iostream>
#import "calc.h"

int main() {
    @autoreleasepool {
        Calc* calc = [[Calc alloc] init];
        int result = [calc add:40 to:2];
        std::cout << "Objective-C++ result is " << result << std::endl;
        // [calc release];
        assert(result == 42);
        return 42 - result;
    }
}
