/*-
 * This file is provided under a dual BSD/GPLv2 license. When using or
 * redistributing this file, you may do so under either license.
 *
 *   BSD LICENSE
 *
 * Copyright 2010-2016 Freescale Semiconductor Inc.
 * Copyright 2017 NXP.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 * * Neither the name of the above-listed copyright holders nor the
 * names of any contributors may be used to endorse or promote products
 * derived from this software without specific prior written permission.
 *
 *   GPL LICENSE SUMMARY
 *
 * ALTERNATIVELY, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") as published by the Free Software
 * Foundation, either version 2 of that License or (at your option) any
 * later version.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#include <inttypes.h>
#include <of.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <error.h>
#include <net/if_arp.h>
#include <assert.h>
#include <unistd.h>

#include <rte_malloc.h>

#include <rte_dpaa_logs.h>
#include <netcfg.h>

/* Structure contains information about all the interfaces given by user
 * on command line.
 */
struct netcfg_interface *netcfg_interface;

/* This data structure contaings all configurations information
 * related to usages of DPA devices.
 */
struct netcfg_info *netcfg;
/* fd to open a socket for making ioctl request to disable/enable shared
 *  interfaces.
 */
static int skfd = -1;

#ifdef RTE_LIBRTE_DPAA_DEBUG_DRIVER
void
dump_netcfg(struct netcfg_info *cfg_ptr)
{
	int i;

	printf("..........  DPAA Configuration  ..........\n\n");

	/* Network interfaces */
	printf("Network interfaces: %d\n", cfg_ptr->num_ethports);
	for (i = 0; i < cfg_ptr->num_ethports; i++) {
		struct fman_if_bpool *bpool;
		struct fm_eth_port_cfg *p_cfg = &cfg_ptr->port_cfg[i];
		struct fman_if *__if = p_cfg->fman_if;

		printf("\n+ Fman %d, MAC %d (%s);\n",
		       __if->fman_idx, __if->mac_idx,
		       (__if->mac_type == fman_mac_1g) ? "1G" : "10G");

		printf("\tmac_addr: " ETH_MAC_PRINTF_FMT "\n",
		       ETH_MAC_PRINTF_ARGS(&__if->mac_addr));

		printf("\ttx_channel_id: 0x%02x\n",
		       __if->tx_channel_id);

		printf("\tfqid_rx_def: 0x%x\n", p_cfg->rx_def);
		printf("\tfqid_rx_err: 0x%x\n", __if->fqid_rx_err);

		printf("\tfqid_tx_err: 0x%x\n", __if->fqid_tx_err);
		printf("\tfqid_tx_confirm: 0x%x\n", __if->fqid_tx_confirm);
		fman_if_for_each_bpool(bpool, __if)
			printf("\tbuffer pool: (bpid=%d, count=%"PRId64
			       " size=%"PRId64", addr=0x%"PRIx64")\n",
			       bpool->bpid, bpool->count, bpool->size,
			       bpool->addr);
	}
}
#endif /* RTE_LIBRTE_DPAA_DEBUG_DRIVER */

static inline int
get_num_netcfg_interfaces(char *str)
{
	char *pch;
	uint8_t count = 0;

	if (str == NULL)
		return -EINVAL;
	pch = strtok(str, ",");
	while (pch != NULL) {
		count++;
		pch = strtok(NULL, ",");
	}
	return count;
}

struct netcfg_info *
netcfg_acquire(void)
{
	struct fman_if *__if;
	int _errno, idx = 0;
	uint8_t num_ports = 0;
	uint8_t num_cfg_ports = 0;
	size_t size;

	/* Extract dpa configuration from fman driver and FMC configuration
	 * for command-line interfaces.
	 */

	if (skfd == -1) {
		/* Open a basic socket to enable/disable shared
		 * interfaces.
		 */
		skfd = socket(AF_PACKET, SOCK_RAW, 0);
		if (unlikely(skfd < 0)) {
			/** ASDF: logging would need to be changed */
			error(0, errno, "%s(): open(SOCK_RAW)", __func__);
			return NULL;
		}
	}

	/* Initialise the Fman driver */
	_errno = fman_init();
	if (_errno) {
		DPAA_BUS_LOG(ERR, "FMAN driver init failed (%d)", errno);
		return NULL;
	}

	/* Number of MAC ports */
	list_for_each_entry(__if, fman_if_list, node)
		num_ports++;

	if (!num_ports) {
		DPAA_BUS_LOG(ERR, "FMAN ports not available");
		return NULL;
	}
	/* Allocate space for all enabled mac ports */
	size = sizeof(*netcfg) +
		(num_ports * sizeof(struct fm_eth_port_cfg));
	/** ASDF: Needs to be changed to rte_malloc */
	netcfg = rte_zmalloc(NULL, size * 1, RTE_CACHE_LINE_SIZE);
	if (unlikely(netcfg == NULL)) {
		DPAA_BUS_LOG(ERR, "Unable to allocat mem for netcfg");
		goto error;
	}

	netcfg->num_ethports = num_ports;

	list_for_each_entry(__if, fman_if_list, node) {
		struct fm_eth_port_cfg *cfg = &netcfg->port_cfg[idx];
		/* Hook in the fman driver interface */
		cfg->fman_if = __if;
		cfg->rx_def = __if->fqid_rx_def;
		num_cfg_ports++;
		idx++;
	}

	if (!num_cfg_ports) {
		DPAA_BUS_LOG(ERR, "No FMAN ports found");
		goto error;
	} else if (num_ports != num_cfg_ports)
		netcfg->num_ethports = num_cfg_ports;

	return netcfg;

error:
	return NULL;
}

void
netcfg_release(struct netcfg_info *cfg_ptr)
{
	rte_free(cfg_ptr);
	/* Close socket for shared interfaces */
	if (skfd >= 0) {
		close(skfd);
		skfd = -1;
	}
}
