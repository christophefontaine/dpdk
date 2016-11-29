/*-
 * Copyright (c) 2016 Solarflare Communications Inc.
 * All rights reserved.
 *
 * This software was jointly developed between OKTET Labs (under contract
 * for Solarflare) and Solarflare Communications, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _SFC_H
#define _SFC_H

#include <stdbool.h>

#include <rte_ethdev.h>
#include <rte_kvargs.h>
#include <rte_spinlock.h>

#include "efx.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SFC_DEV_TO_PCI(eth_dev) \
	RTE_DEV_TO_PCI((eth_dev)->device)

/*
 * +---------------+
 * | UNINITIALIZED |<-----------+
 * +---------------+		|
 *	|.eth_dev_init		|.eth_dev_uninit
 *	V			|
 * +---------------+------------+
 * |  INITIALIZED  |
 * +---------------+<-----------+
 *	|.dev_configure		|
 *	V			|
 * +---------------+		|
 * |  CONFIGURING  |------------^
 * +---------------+ failed	|
 *	|success		|
 *	|		+---------------+
 *	|		|    CLOSING    |
 *	|		+---------------+
 *	|			^
 *	V			|.dev_close
 * +---------------+------------+
 * |  CONFIGURED   |
 * +---------------+<-----------+
 *	|.dev_start		|
 *	V			|
 * +---------------+		|
 * |   STARTING    |------------^
 * +---------------+ failed	|
 *	|success		|
 *	|		+---------------+
 *	|		|   STOPPING    |
 *	|		+---------------+
 *	|			^
 *	V			|.dev_stop
 * +---------------+------------+
 * |    STARTED    |
 * +---------------+
 */
enum sfc_adapter_state {
	SFC_ADAPTER_UNINITIALIZED = 0,
	SFC_ADAPTER_INITIALIZED,
	SFC_ADAPTER_CONFIGURING,
	SFC_ADAPTER_CONFIGURED,
	SFC_ADAPTER_CLOSING,
	SFC_ADAPTER_STARTING,
	SFC_ADAPTER_STARTED,
	SFC_ADAPTER_STOPPING,

	SFC_ADAPTER_NSTATES
};

enum sfc_mcdi_state {
	SFC_MCDI_UNINITIALIZED = 0,
	SFC_MCDI_INITIALIZED,
	SFC_MCDI_BUSY,
	SFC_MCDI_COMPLETED,

	SFC_MCDI_NSTATES
};

struct sfc_mcdi {
	rte_spinlock_t			lock;
	efsys_mem_t			mem;
	enum sfc_mcdi_state		state;
	efx_mcdi_transport_t		transport;
};

struct sfc_intr {
	efx_intr_type_t			type;
};

/* Adapter private data */
struct sfc_adapter {
	/*
	 * PMD setup and configuration is not thread safe. Since it is not
	 * performance sensitive, it is better to guarantee thread-safety
	 * and add device level lock. Adapter control operations which
	 * change its state should acquire the lock.
	 */
	rte_spinlock_t			lock;
	enum sfc_adapter_state		state;
	struct rte_eth_dev		*eth_dev;
	struct rte_kvargs		*kvargs;
	bool				debug_init;
	int				socket_id;
	efsys_bar_t			mem_bar;
	efx_family_t			family;
	efx_nic_t			*nic;
	rte_spinlock_t			nic_lock;

	struct sfc_mcdi			mcdi;
	struct sfc_intr			intr;

	unsigned int			rxq_max;
	unsigned int			txq_max;
};

/*
 * Add wrapper functions to acquire/release lock to be able to remove or
 * change the lock in one place.
 */

static inline void
sfc_adapter_lock_init(struct sfc_adapter *sa)
{
	rte_spinlock_init(&sa->lock);
}

static inline int
sfc_adapter_is_locked(struct sfc_adapter *sa)
{
	return rte_spinlock_is_locked(&sa->lock);
}

static inline void
sfc_adapter_lock(struct sfc_adapter *sa)
{
	rte_spinlock_lock(&sa->lock);
}

static inline void
sfc_adapter_unlock(struct sfc_adapter *sa)
{
	rte_spinlock_unlock(&sa->lock);
}

static inline void
sfc_adapter_lock_fini(__rte_unused struct sfc_adapter *sa)
{
	/* Just for symmetry of the API */
}

int sfc_dma_alloc(const struct sfc_adapter *sa, const char *name, uint16_t id,
		  size_t len, int socket_id, efsys_mem_t *esmp);
void sfc_dma_free(const struct sfc_adapter *sa, efsys_mem_t *esmp);

int sfc_attach(struct sfc_adapter *sa);
void sfc_detach(struct sfc_adapter *sa);
int sfc_start(struct sfc_adapter *sa);
void sfc_stop(struct sfc_adapter *sa);

int sfc_mcdi_init(struct sfc_adapter *sa);
void sfc_mcdi_fini(struct sfc_adapter *sa);

int sfc_configure(struct sfc_adapter *sa);
void sfc_close(struct sfc_adapter *sa);

int sfc_intr_attach(struct sfc_adapter *sa);
void sfc_intr_detach(struct sfc_adapter *sa);
int sfc_intr_init(struct sfc_adapter *sa);
void sfc_intr_fini(struct sfc_adapter *sa);
int sfc_intr_start(struct sfc_adapter *sa);
void sfc_intr_stop(struct sfc_adapter *sa);

#ifdef __cplusplus
}
#endif

#endif  /* _SFC_H */