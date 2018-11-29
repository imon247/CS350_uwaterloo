/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <spl.h>
#include <spinlock.h>
#include <proc.h>
#include <current.h>
#include <mips/tlb.h>
#include <addrspace.h>
#include <vm.h>
#include <mips/vm.h>
#include "opt-A3.h"

/*
 * Dumb MIPS-only "VM system" that is intended to only be just barely
 * enough to struggle off the ground.
 */

/* under dumbvm, always have 48k of user stack */
#define DUMBVM_STACKPAGES    12

/*
 * Wrap rma_stealmem in a spinlock.
 */
//static struct spinlock stealmem_lock = SPINLOCK_INITIALIZER;
/*
struct core_map{
	bool used;
	bool contiguous;
	paddr_t addr;
};
*/    // this is the core_map struct in vm.h
struct core_map *cm;
unsigned int num_of_frame;


void
vm_bootstrap(void)
{
#if OPT_A3
	
	paddr_t lo, hi;
	ram_getsize(&lo, &hi);
	
	//kprintf("lo: %d, hi: %d, The ramsize: %d\n", lo, hi, (unsigned int)(hi-lo));
	//kprintf("PAGE_SIZE: %d\n", (unsigned int)PAGE_SIZE);
	
	cm = (struct core_map*)PADDR_TO_KVADDR(lo);
	
	num_of_frame = (hi-lo)/PAGE_SIZE;
	//kprintf("number of frames: %d\n\n", num_of_frame);
	
	paddr_t actual_lo = lo;
	actual_lo += num_of_frame*(sizeof(struct core_map));
	while(actual_lo%PAGE_SIZE!=0) actual_lo++;
	
	//kprintf("size of core_map: %d\n", actual_lo-lo);
	
	num_of_frame = (hi-actual_lo)/PAGE_SIZE;
	//kprintf("New ramsize: %d\n", hi-actual_lo);
	//kprintf("number of frames: %d\n\n", num_of_frame);
	
	for(unsigned int i=0;i<num_of_frame;i++){
		cm[i].used = false;
		cm[i].contiguous = false;
		cm[i].addr = actual_lo;
		//kprintf("cm[%d].addr = %d", i, cm[i].addr);
		actual_lo += PAGE_SIZE;
	}
	//kprintf("\n");
	bootstrap_finished = true;
	
	
	
	/*
	paddr_t temp_lo = lo;
	temp_lo += num_of_frame*(sizeof(struct core_map));
	kprintf("size of core_map: %d\n", num_of_frame*(sizeof(struct core_map)));
	while(temp_lo%PAGE_SIZE != 0) temp_lo++;
	kprintf("temp_lo = %d\n", temp_lo);
	*/
	
#endif
}

static
paddr_t
getppages(unsigned long npages)
{
#if OPT_A3
	paddr_t addr;
	//spinlock_acquire(&stealmem_lock);
	if(bootstrap_finished){
		paddr_t lo, hi;
		ram_getsize(&lo, &hi);
		//kprintf("alloc_kpages: bootstrap finished\n");
	
		int start = 0;
	
	look1:
		//kprintf("%d pages are required\n", (int)npages);
		for(unsigned int i=start;i<start+npages;i++){
			if(i >= num_of_frame){
				//spinlock_release(&stealmem_lock);
				return ENOMEM;
			}
			if(cm[i].used == true){
				start = i+1;
				goto look1;
			}
		}
		//kprintf("I have found a continuous region in physical memory\n");
		kprintf("the index of the first page: %d\n", start);
		kprintf("num of pages needed: %d, pages available: %d\n", (int)npages, (int)num_of_frame);
		for(unsigned int i=start;i<start+npages;i++){
			cm[i].used = true;
			if(i==start+npages-1) cm[i].contiguous = false;
			else cm[i].contiguous = true;
		}
		kprintf("getpage finished, vaddr = %d\n", cm[start].addr);
		//spinlock_release(&stealmem_lock);
		addr = cm[start].addr;
	}
	
	else{
		addr = ram_stealmem(npages);
		
		//spinlock_release(&stealmem_lock);
	}
	return addr;
	
#else
	paddr_t addr;

	spinlock_acquire(&stealmem_lock);

	addr = ram_stealmem(npages);
	
	spinlock_release(&stealmem_lock);
	return addr;
#endif
}

/* Allocate/free some kernel-space virtual pages */
vaddr_t 
alloc_kpages(int npages)		// npages = number of pages needed by kmalloc or addrspace
{
#if OPT_A3
	paddr_t pa;
	pa = getppages(npages);
	if(pa==ENOMEM) return ENOMEM;
	//if(pa==0) return 0;
	return PADDR_TO_KVADDR(pa);
#else

	paddr_t pa;
	pa = getppages(npages);
	if (pa==0) {
		return 0;
	}
	return PADDR_TO_KVADDR(pa);
#endif
}

void 
free_kpages(vaddr_t addr)
{
#if OPT_A3
	//spinlock_acquire(&stealmem_lock);
	if(bootstrap_finished){
		
		for(unsigned int i=0;i<num_of_frame;i++){
			kprintf(".");
			if(cm[i].addr!=addr) continue;
			
			bool contiguous = cm[i].contiguous;
			//int j=i+1;
			//cm[i].used = false;
			//cm[i].contiguous = false;
			while(contiguous){
				cm[i].used = false;
				contiguous = cm[i+1].contiguous;
				cm[i].contiguous = false;
				i++;
			}
			cm[i].used = false;
			cm[i].contiguous = false;
			break;
		}
		
		
	}
	//spinlock_release(&stealmem_lock);


#else
	/* nothing - leak the memory. */

	(void)addr;
#endif
}

void
vm_tlbshootdown_all(void)
{
	panic("dumbvm tried to do tlb shootdown?!\n");
}

void
vm_tlbshootdown(const struct tlbshootdown *ts)
{
	(void)ts;
	panic("dumbvm tried to do tlb shootdown?!\n");
}

int
vm_fault(int faulttype, vaddr_t faultaddress)		// faultaddress is simply the virtual address!
{
	vaddr_t vbase1, vtop1, vbase2, vtop2, stackbase, stacktop;
	paddr_t paddr;
	int i;
	uint32_t ehi, elo;
	struct addrspace *as;
	int spl;

	faultaddress &= PAGE_FRAME;

	DEBUG(DB_VM, "dumbvm: fault: 0x%x\n", faultaddress);

	switch (faulttype) {
	    case VM_FAULT_READONLY:
		/* We always create pages read-write, so we can't get this */
	#if OPT_A3
		//kprintf("vm_fault() case READONLY\n");
		return 1;
	#else
		panic("dumbvm: got VM_FAULT_READONLY\n");
	#endif
	    case VM_FAULT_READ:
	    case VM_FAULT_WRITE:
		break;
	    default:
		return EINVAL;
	}

	if (curproc == NULL) {
		/*
		 * No process. This is probably a kernel fault early
		 * in boot. Return EFAULT so as to panic instead of
		 * getting into an infinite faulting loop.
		 */
		return EFAULT;
	}

	as = curproc_getas();
	if (as == NULL) {
		/*
		 * No address space set up. This is probably also a
		 * kernel fault early in boot.
		 */
		return EFAULT;
	}

	/* Assert that the address space has been set up properly. */
	KASSERT(as->as_vbase1 != 0);
	KASSERT(as->as_pbase1 != 0);
	KASSERT(as->as_npages1 != 0);
	KASSERT(as->as_vbase2 != 0);
	KASSERT(as->as_pbase2 != 0);
	KASSERT(as->as_npages2 != 0);
	KASSERT(as->as_stackpbase != 0);
	KASSERT((as->as_vbase1 & PAGE_FRAME) == as->as_vbase1);
	KASSERT((as->as_pbase1 & PAGE_FRAME) == as->as_pbase1);
	KASSERT((as->as_vbase2 & PAGE_FRAME) == as->as_vbase2);
	KASSERT((as->as_pbase2 & PAGE_FRAME) == as->as_pbase2);
	KASSERT((as->as_stackpbase & PAGE_FRAME) == as->as_stackpbase);

	vbase1 = as->as_vbase1;
	vtop1 = vbase1 + as->as_npages1 * PAGE_SIZE;
	vbase2 = as->as_vbase2;
	vtop2 = vbase2 + as->as_npages2 * PAGE_SIZE;
	stackbase = USERSTACK - DUMBVM_STACKPAGES * PAGE_SIZE;
	stacktop = USERSTACK;

	if (faultaddress >= vbase1 && faultaddress < vtop1) {
		paddr = (faultaddress - vbase1) + as->as_pbase1;
	}
	else if (faultaddress >= vbase2 && faultaddress < vtop2) {
		paddr = (faultaddress - vbase2) + as->as_pbase2;
	}
	else if (faultaddress >= stackbase && faultaddress < stacktop) {
		paddr = (faultaddress - stackbase) + as->as_stackpbase;
	}
	else {
		return EFAULT;
	}

	/* make sure it's page-aligned */
	KASSERT((paddr & PAGE_FRAME) == paddr);

	/* Disable interrupts on this CPU while frobbing the TLB. */
	spl = splhigh();

	for (i=0; i<NUM_TLB; i++) {
		tlb_read(&ehi, &elo, i);
		if (elo & TLBLO_VALID) {
			continue;
		}
		ehi = faultaddress;
		elo = paddr | TLBLO_DIRTY | TLBLO_VALID;
		if (as->load_complete==1 && faultaddress >= vbase1 && faultaddress < vtop1){
		//kprintf("disable writing to code segment\n");
		elo &= ~TLBLO_DIRTY;
		}
		DEBUG(DB_VM, "dumbvm: 0x%x -> 0x%x\n", faultaddress, paddr);
		tlb_write(ehi, elo, i);
		splx(spl);
		return 0;
	}
#if OPT_A3
	ehi = faultaddress;
	elo = paddr | TLBLO_DIRTY | TLBLO_VALID;
	DEBUG(DB_VM, "dumbvm: 0x%x -> 0x%x\n", faultaddress, paddr);
	if (as->load_complete==1 && faultaddress >= vbase1 && faultaddress < vtop1){
		//kprintf("disable writing to code segment\n");
		elo &= ~TLBLO_DIRTY;
	}
	tlb_random(ehi, elo);		// entryhigh, entrylow
	splx(spl);
	return 0;
	
#else
	kprintf("dumbvm: Ran out of TLB entries - cannot handle page fault\n");
	splx(spl);
	return EFAULT;
#endif
}

struct addrspace *
as_create(void)
{
	struct addrspace *as = kmalloc(sizeof(struct addrspace));
	if (as==NULL) {
		return NULL;
	}
#if OPT_A3
	
	as->as_vbase1 = 0;
	as->as_pbase1 = 0;
	as->as_npages1 = 0;
	as->as_vbase2 = 0;
	as->as_pbase2 = 0;
	as->as_npages2 = 0;
	as->as_stackpbase = 0;
	
	as->load_complete = 0;
	as->readable = 0;
	as->writeable = 0;
	as->executable = 0;
#else
	as->as_vbase1 = 0;
	as->as_pbase1 = 0;
	as->as_npages1 = 0;
	as->as_vbase2 = 0;
	as->as_pbase2 = 0;
	as->as_npages2 = 0;
	as->as_stackpbase = 0;
#endif
	return as;
}

void
as_destroy(struct addrspace *as)
{
#if OPT_A3
	free_kpages(as->as_pbase1);
	free_kpages(as->as_pbase2);
	free_kpages(as->as_stackpbase);
#endif
	kfree(as);
}

void
as_activate(void)
{
	int i, spl;
	struct addrspace *as;

	as = curproc_getas();
#ifdef UW
        /* Kernel threads don't have an address spaces to activate */
#endif
	if (as == NULL) {
		return;
	}

	/* Disable interrupts on this CPU while frobbing the TLB. */
	spl = splhigh();

	for (i=0; i<NUM_TLB; i++) {
		tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
	}

	splx(spl);
}

void
as_deactivate(void)
{
	/* nothing */
}

int
as_define_region(struct addrspace *as, vaddr_t vaddr, size_t sz,
		 int readable, int writeable, int executable)
{
	size_t npages; 

	/* Align the region. First, the base... */
	/* PAGE_FRAME is the mask for getting page number from addr, hence ~PAGE_FRAME is the mask 
	   for getting offset within a page from vaddr */
	sz += vaddr & ~(vaddr_t)PAGE_FRAME;		
	vaddr &= PAGE_FRAME;

	/* ...and now the length. */
	sz = (sz + PAGE_SIZE - 1) & PAGE_FRAME;

	npages = sz / PAGE_SIZE;

#if OPT_A3
	as->readable = readable;
	as->writeable = writeable;
	as->executable = executable;
#endif

	if (as->as_vbase1 == 0) {
		as->as_vbase1 = vaddr;
		as->as_npages1 = npages;
		return 0;
	}

	if (as->as_vbase2 == 0) {
		as->as_vbase2 = vaddr;
		as->as_npages2 = npages;
		return 0;
	}
	
	//kprintf("--as_vbase1: %d; as_vbase2: %d\n", as->as_vbase1, as->as_vbase2);
	/*
	 * Support for more than two regions is not available.
	 */
	kprintf("dumbvm: Warning: too many regions\n");
	return EUNIMP;
}

static
void
as_zero_region(paddr_t paddr, unsigned npages)
{
	bzero((void *)PADDR_TO_KVADDR(paddr), npages * PAGE_SIZE);
}

int
as_prepare_load(struct addrspace *as)
{
	KASSERT(as->as_pbase1 == 0);
	KASSERT(as->as_pbase2 == 0);
	KASSERT(as->as_stackpbase == 0);

	as->as_pbase1 = getppages(as->as_npages1);
	if (as->as_pbase1 == 0) {
		return ENOMEM;
	}

	as->as_pbase2 = getppages(as->as_npages2);
	if (as->as_pbase2 == 0) {
		return ENOMEM;
	}

	as->as_stackpbase = getppages(DUMBVM_STACKPAGES);
	if (as->as_stackpbase == 0) {
		return ENOMEM;
	}
	
	as_zero_region(as->as_pbase1, as->as_npages1);
	as_zero_region(as->as_pbase2, as->as_npages2);
	as_zero_region(as->as_stackpbase, DUMBVM_STACKPAGES);

	return 0;
}

int
as_complete_load(struct addrspace *as)
{
#if OPT_A3
	for (int i=0; i<NUM_TLB; i++) {
		tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
	}
	
	as->load_complete = 1;
#else	
	(void)as;
#endif
	return 0;

}

int
as_define_stack(struct addrspace *as, vaddr_t *stackptr)
{
	KASSERT(as->as_stackpbase != 0);

	*stackptr = USERSTACK;
	return 0;
}

int
as_copy(struct addrspace *old, struct addrspace **ret)
{
	struct addrspace *new;

	new = as_create();
	if (new==NULL) {
		return ENOMEM;
	}

	new->as_vbase1 = old->as_vbase1;
	new->as_npages1 = old->as_npages1;
	new->as_vbase2 = old->as_vbase2;
	new->as_npages2 = old->as_npages2;

	/* (Mis)use as_prepare_load to allocate some physical memory. */
	if (as_prepare_load(new)) {
		as_destroy(new);
		return ENOMEM;
	}

	KASSERT(new->as_pbase1 != 0);
	KASSERT(new->as_pbase2 != 0);
	KASSERT(new->as_stackpbase != 0);

	memmove((void *)PADDR_TO_KVADDR(new->as_pbase1),
		(const void *)PADDR_TO_KVADDR(old->as_pbase1),
		old->as_npages1*PAGE_SIZE);

	memmove((void *)PADDR_TO_KVADDR(new->as_pbase2),
		(const void *)PADDR_TO_KVADDR(old->as_pbase2),
		old->as_npages2*PAGE_SIZE);

	memmove((void *)PADDR_TO_KVADDR(new->as_stackpbase),
		(const void *)PADDR_TO_KVADDR(old->as_stackpbase),
		DUMBVM_STACKPAGES*PAGE_SIZE);
	
	*ret = new;
	return 0;
}
