/*
 * Copyright 2018 Google LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdalign.h>
#include <stddef.h>
#include <stdnoreturn.h>

#include "hf/api.h"
#include "hf/boot_params.h"
#include "hf/cpio.h"
#include "hf/cpu.h"
#include "hf/dlog.h"
#include "hf/load.h"
#include "hf/mm.h"
#include "hf/mpool.h"
#include "hf/std.h"
#include "hf/vm.h"

#include "vmapi/hf/call.h"

alignas(alignof(
	struct mm_page_table)) char ptable_buf[sizeof(struct mm_page_table) *
					       HEAP_PAGES];

/**
 * Blocks the hypervisor.
 *
 * TODO: Determine if we want to omit strings on non-debug builds.
 */
noreturn void panic(const char *fmt, ...)
{
	va_list args;

	/* TODO: Block all CPUs. */

	dlog("Panic: ");

	va_start(args, fmt);
	vdlog(fmt, args);
	va_end(args);

	dlog("\n");

	for (;;) {
	}
}

/**
 * Performs one-time initialisation of the hypervisor.
 */
static void one_time_init(void)
{
	struct boot_params params;
	struct boot_params_update update;
	struct memiter primary_initrd;
	struct memiter cpio;
	void *initrd;
	size_t i;
	struct mpool ppool;

	dlog("Initialising hafnium\n");

	mpool_init(&ppool, sizeof(struct mm_page_table));
	mpool_add_chunk(&ppool, ptable_buf, sizeof(ptable_buf));

	cpu_module_init();

	if (!mm_init(&ppool)) {
		panic("mm_init failed");
	}

	/* Enable locks now that mm is initialised. */
	dlog_enable_lock();
	mpool_enable_locks();

	if (!plat_get_boot_params(&params, &ppool)) {
		panic("unable to retrieve boot params");
	}

	for (i = 0; i < params.mem_ranges_count; ++i) {
		dlog("Memory range:  0x%x - 0x%x\n",
		     pa_addr(params.mem_ranges[i].begin),
		     pa_addr(params.mem_ranges[i].end) - 1);
	}

	dlog("Ramdisk range: 0x%x - 0x%x\n", pa_addr(params.initrd_begin),
	     pa_addr(params.initrd_end) - 1);

	/* Map initrd in, and initialise cpio parser. */
	initrd = mm_identity_map(params.initrd_begin, params.initrd_end,
				 MM_MODE_R, &ppool);
	if (!initrd) {
		panic("unable to map initrd in");
	}

	memiter_init(&cpio, initrd,
		     pa_addr(params.initrd_end) - pa_addr(params.initrd_begin));

	/* Load all VMs. */
	if (!load_primary(&cpio, params.kernel_arg, &primary_initrd, &ppool)) {
		panic("unable to load primary VM");
	}

	/*
	 * load_secondary will add regions assigned to the secondary VMs from
	 * mem_ranges to reserved_ranges.
	 */
	update.initrd_begin = pa_from_va(va_from_ptr(primary_initrd.next));
	update.initrd_end = pa_from_va(va_from_ptr(primary_initrd.limit));
	update.reserved_ranges_count = 0;
	if (!load_secondary(&cpio, &params, &update, &ppool)) {
		panic("unable to load secondary VMs");
	}

	/* Prepare to run by updating bootparams as seen by primary VM. */
	if (!plat_update_boot_params(&update, &ppool)) {
		panic("plat_update_boot_params failed");
	}

	mm_defrag(&ppool);

	/* Initialise the API page pool. ppool will be empty from now on. */
	api_init(&ppool);

	/* Enable TLB invalidation for VM page table updates. */
	mm_vm_enable_invalidation();

	dlog("Hafnium initialisation completed\n");
}

/**
 * The entry point of CPUs when they are turned on. It is supposed to initialise
 * all state and return the first vCPU to run.
 */
struct vcpu *cpu_main(struct cpu *c)
{
	struct vcpu *vcpu;

	/*
	 * Do global one-time initialisation just once. We avoid using atomics
	 * by only touching the variable from cpu 0.
	 */
	static volatile bool inited = false;

	if (cpu_index(c) == 0 && !inited) {
		inited = true;
		one_time_init();
	}

	dlog("Starting up cpu %d\n", cpu_index(c));

	if (!mm_cpu_init()) {
		panic("mm_cpu_init failed");
	}

	vcpu = &vm_get(HF_PRIMARY_VM_ID)->vcpus[cpu_index(c)];
	vcpu->cpu = c;
	return vcpu;
}
