/**
 * $Id$
 * Copyright (C) 2008 - 2014 Nils Asmussen
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#pragma once

#include <esc/common.h>
#include <sys/arch/mmix/mem/layout.h>
#include <sys/arch/mmix/mem/addrspace.h>
#include <sys/mem/physmemareas.h>
#include <string.h>
#include <assert.h>

/* converts a virtual address to the page-directory-index for that address */
#define ADDR_TO_PDINDEX(addr)	((size_t)((uintptr_t)(addr) / PAGE_SIZE / PT_ENTRY_COUNT))

/* converts a virtual address to the index in the corresponding page-table */
#define ADDR_TO_PTINDEX(addr)	((size_t)(((uintptr_t)(addr) / PAGE_SIZE) % PT_ENTRY_COUNT))

/* converts pages to page-tables (how many page-tables are required for the pages?) */
#define PAGES_TO_PTS(pageCount)	(((size_t)(pageCount) + (PT_ENTRY_COUNT - 1)) / PT_ENTRY_COUNT)

/* determines whether the given address is on the heap */
/* in this case it is sufficient to check whether its not in the data-area of the kernel */
#define IS_ON_HEAP(addr) 		((uintptr_t)(addr) > KERNEL_START + Boot::getKernelSize())

/* determines whether the given address is in a shared kernel area; in this case it is "shared"
 * if it is accessed over the directly mapped space. */
#define IS_SHARED(addr)			((uintptr_t)(addr) >= KERNEL_START)

/*
 * PTE:
 * +-------+-+-------------------------------------------+----------+-+-+-+
 * |       |e|                 frameNumber               |     n    |r|w|x|
 * +-------+-+-------------------------------------------+----------+-+-+-+
 * 63     57 56                                          13         3     0
 *
 * PTP:
 * +-+---------------------------------------------------+----------+-+-+-+
 * |1|                   ptframeNumber                   |     n    | ign |
 * +-+---------------------------------------------------+----------+-+-+-+
 * 63                                                    13         3     0
 */

/* to shift a flag down to the first bit */
#define PG_PRESENT_SHIFT			0
#define PG_WRITABLE_SHIFT			1
#define PG_EXECUTABLE_SHIFT			2

/* pte fields */
#define PTE_EXISTS					(1UL << 56)
#define PTE_READABLE				(1UL << 2)
#define PTE_WRITABLE				(1UL << 1)
#define PTE_EXECUTABLE				(1UL << 0)
#define PTE_FRAMENO(pte)			(((pte) >> PAGE_BITS) & 0x7FFFFFFFFFF)
#define PTE_FRAMENO_MASK			0x00FFFFFFFFFFE000
#define PTE_NMASK					0x0000000000001FF8

#define PAGE_NO(virt)				(((uintptr_t)(virt) & 0x1FFFFFFFFFFFFFFF) >> PAGE_BITS)
#define SEGSIZE(rV,i)				((i) == 0 ? 0 : (((rV) >> (64 - (i) * 4)) & 0xF))

class PageDir : public PageDirBase {
	friend class PageDirBase;

public:
	explicit PageDir() : PageDirBase(), addrSpace(), rv(), ptables() {
	}

	uint64_t getRV() const {
		return rv;
	}
	void setRV(uint64_t nrv) {
		rv = nrv;
	}
	AddressSpace *getAddrSpace() {
		return addrSpace;
	}
	void setAddrSpace(AddressSpace *aspace) {
		addrSpace = aspace;
	}

private:
	static void clearTC() {
		asm volatile ("SYNC 6");
	}
	static void updateTC(uint64_t key) {
		asm volatile ("LDVTS %0,%0,0" : "+r"(key));
	}
	static void setrV(uint64_t rv) {
		asm volatile ("PUT rV,%0" : : "r"(rv));
	}

	static size_t getPageCountOf(uint64_t *pt,size_t level);
	static void printPageTable(OStream &os,ulong seg,uintptr_t addr,uint64_t *pt,size_t level,ulong indent);
	static void printPTE(OStream &os,uint64_t pte);

	uint64_t *getPT(uintptr_t virt,bool create,size_t *createdPts) const;
	uint64_t getPTE(uintptr_t virt) const;
	size_t removePts(uint64_t pageNo,uint64_t c,ulong level,ulong depth);
	size_t remEmptyPts(uintptr_t virt);
	void tcRemPT(uintptr_t virt);

private:
	AddressSpace *addrSpace;
	uint64_t rv;
	ulong ptables;
	static PageDir firstCon;
};

inline uintptr_t PageDirBase::getPhysAddr() const {
	const PageDir *pdir = static_cast<const PageDir*>(this);
	return pdir->rv & 0xFFFFFFE000;
}

inline void PageDirBase::makeFirst() {
	PageDir *pdir = static_cast<PageDir*>(this);
	pdir->addrSpace = PageDir::firstCon.addrSpace;
	pdir->rv = PageDir::firstCon.rv;
	pdir->ptables = PageDir::firstCon.ptables;
}

inline uintptr_t PageDirBase::makeAccessible(uintptr_t phys,size_t pages) {
	assert(phys == 0);
	return DIR_MAP_AREA | (PhysMemAreas::alloc(pages) * PAGE_SIZE);
}

inline bool PageDirBase::isInUserSpace(uintptr_t virt,size_t count) {
	return virt + count <= DIR_MAP_AREA && virt + count >= virt;
}

inline bool PageDirBase::isPresent(uintptr_t virt) const {
	const PageDir *pdir = static_cast<const PageDir*>(this);
	uint64_t pte = pdir->getPTE(virt);
	return pte & PTE_EXISTS;
}

inline frameno_t PageDirBase::getFrameNo(uintptr_t virt) const {
	const PageDir *pdir = static_cast<const PageDir*>(this);
	uint64_t pte = pdir->getPTE(virt);
	assert(pte & PTE_EXISTS);
	return PTE_FRAMENO(pte);
}

inline uintptr_t PageDirBase::getAccess(frameno_t frame) {
	return frame * PAGE_SIZE | DIR_MAP_AREA;
}

inline void PageDirBase::removeAccess(A_UNUSED frameno_t frame) {
	/* nothing to do */
}

inline void PageDirBase::copyToFrame(frameno_t frame,const void *src) {
	memcpy((void*)(frame * PAGE_SIZE | DIR_MAP_AREA),src,PAGE_SIZE);
}

inline void PageDirBase::copyFromFrame(frameno_t frame,void *dst) {
	memcpy(dst,(void*)(frame * PAGE_SIZE | DIR_MAP_AREA),PAGE_SIZE);
}
