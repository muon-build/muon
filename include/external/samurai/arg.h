/*
 * SPDX-FileCopyrightText: Michael Forney <mforney@mforney.org>
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: MIT
 */

#ifndef MUON_EXTERNAL_SAMU_ARG_H
#define MUON_EXTERNAL_SAMU_ARG_H

#define SAMU_ARGBEGIN \
	for (;;) { \
		if (argc > 0) \
			++argv, --argc; \
		if (argc == 0 || (*argv)[0] != '-') \
			break; \
		if ((*argv)[1] == '-' && !(*argv)[2]) { \
			++argv, --argc; \
			break; \
		} \
		for (char *opt_ = &(*argv)[1], done_ = 0; !done_ && *opt_; ++opt_) { \
			switch (*opt_)

#define SAMU_ARGEND \
		} \
	}

#define SAMU_EARGF(x) \
	(done_ = 1, *++opt_ ? opt_ : argv[1] ? --argc, *++argv : ((x), abort(), (char *)0))

#endif
