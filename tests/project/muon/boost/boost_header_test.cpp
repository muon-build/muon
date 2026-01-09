/*
 * SPDX-FileCopyrightText: VaiTon <eyadlorenzo@gmail.com>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <boost/assert.hpp>
#include <boost/config.hpp>
#include <boost/static_assert.hpp>
#include <boost/type_traits/is_floating_point.hpp>
#include <boost/type_traits/is_integral.hpp>
#include <boost/version.hpp>
#include <iostream>

int
main(void)
{
	std::cout << "Boost version: " << BOOST_VERSION / 100000 << "." << (BOOST_VERSION / 100) % 1000 << "."
		  << BOOST_VERSION % 100 << std::endl;

	std::cout << "Boost lib version: " << BOOST_LIB_VERSION << std::endl;

	BOOST_STATIC_ASSERT(boost::is_integral<int>::value);
	BOOST_STATIC_ASSERT(boost::is_integral<long>::value);
	BOOST_STATIC_ASSERT(!boost::is_integral<float>::value);
	BOOST_STATIC_ASSERT(boost::is_floating_point<double>::value);
	BOOST_STATIC_ASSERT(!boost::is_floating_point<int>::value);

	int x = 42;
	BOOST_ASSERT(x == 42);
	BOOST_ASSERT(x > 0);

#ifdef BOOST_PLATFORM
	std::cout << "Platform detected: " << BOOST_PLATFORM << std::endl;
#endif

#ifdef BOOST_COMPILER
	std::cout << "Compiler detected: " << BOOST_COMPILER << std::endl;
#endif

	// Test type trait results at runtime
	if (boost::is_integral<int>::value) {
		std::cout << "Type traits working correctly" << std::endl;
	} else {
		std::cerr << "Type traits not working" << std::endl;
		return 1;
	}

	std::cout << "Boost header-only test passed" << std::endl;

	return 0;
}
