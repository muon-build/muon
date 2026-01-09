/*
 * SPDX-FileCopyrightText: VaiTon <eyadlorenzo@gmail.com>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <pcap.h>
#include <pcap/pcap.h>

int
main(void)
{
	pcap_if_t *alldevs;
	if (pcap_findalldevs(&alldevs, NULL) == -1) {
		return 1;
	}
	return 0;
}
