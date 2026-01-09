/*
 * SPDX-FileCopyrightText: VaiTon <eyadlorenzo@gmail.com>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <boost/filesystem.hpp>
#include <boost/version.hpp>
#include <iostream>

namespace fs = boost::filesystem;

int
main(void)
{
	// Test that Boost.Filesystem links correctly
	// Note: boost_system is header-only in modern Boost (1.69+)

	// Test basic path operations (header-only functionality)
	fs::path p("/tmp/test.txt");

	if (p.filename() != "test.txt") {
		std::cerr << "Path filename extraction failed" << std::endl;
		return 1;
	}

	if (p.parent_path() != "/tmp") {
		std::cerr << "Path parent_path extraction failed" << std::endl;
		return 1;
	}

	try {
		fs::path current = fs::current_path();

		// Verify current path is absolute
		if (!current.is_absolute()) {
			std::cerr << "Current path is not absolute" << std::endl;
			return 1;
		}

		std::cout << "Current directory: " << current << std::endl;
	} catch (const fs::filesystem_error &e) {
		std::cerr << "Filesystem error: " << e.what() << std::endl;
		return 1;
	}

	fs::path self_path = fs::current_path();
	if (!fs::exists(self_path)) {
		std::cerr << "Current directory should exist" << std::endl;
		return 1;
	}

	std::cout << "Boost.Filesystem test passed (Boost version: " << BOOST_VERSION / 100000 << "."
		  << (BOOST_VERSION / 100) % 1000 << "." << BOOST_VERSION % 100 << ")" << std::endl;

	return 0;
}
