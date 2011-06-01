/**
 * $Id$
 * Copyright (C) 2008 - 2009 Nils Asmussen
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

#include <sys/common.h>
#include <sys/task/thread.h>
#include <sys/task/proc.h>
#include <sys/task/signals.h>
#include <sys/task/event.h>
#include <sys/task/timer.h>
#include <sys/vfs/vfs.h>
#include <sys/vfs/info.h>
#include <sys/vfs/node.h>
#include <sys/vfs/real.h>
#include <sys/mem/kheap.h>
#include <sys/mem/paging.h>
#include <sys/mem/pmem.h>
#include <sys/mem/swap.h>
#include <sys/mem/vmm.h>
#include <sys/task/sched.h>
#include <sys/task/lock.h>
#include <sys/util.h>
#include <sys/video.h>
#include <assert.h>
#include <string.h>
#include <errors.h>

static sThread *thread_createInitial(sProc *p,eThreadState state);
static tTid thread_getFreeTid(void);
static bool thread_add(sThread *t);
static void thread_remove(sThread *t);

/* our threads */
static sSLList *threads;
static sThread *tidToThread[MAX_THREAD_COUNT];
static sThread *cur = NULL;
static tTid nextTid = 0;

/* list of dead threads that should be destroyed */
static sSLList* deadThreads = NULL;

sThread *thread_init(sProc *p) {
	threads = sll_create();
	if(!threads)
		util_panic("Unable to create thread-list");

	/* create thread for init */
	cur = thread_createInitial(p,ST_RUNNING);
	return cur;
}

static sThread *thread_createInitial(sProc *p,eThreadState state) {
	sThread *t = (sThread*)kheap_alloc(sizeof(sThread));
	if(t == NULL)
		util_panic("Unable to allocate mem for initial thread");

	t->state = state;
	t->events = 0;
	t->ignoreSignals = 0;
	t->tid = nextTid++;
	t->proc = p;
	/* we'll give the thread a stack later */
	t->stats.ucycleCount.val64 = 0;
	t->stats.ucycleStart = 0;
	t->stats.kcycleCount.val64 = 0;
	t->stats.kcycleStart = 0;
	t->stats.schedCount = 0;
	t->stats.syscalls = 0;
	/* TODO */
#ifdef __i586__
	t->fpuState = NULL;
#endif
	t->stackRegion = -1;
	t->tlsRegion = -1;

	/* create list */
	if(!thread_add(t))
		util_panic("Unable to put initial thread into the thread-list");

	/* insert in VFS; thread needs to be inserted for it */
	if(!vfs_createThread(t->tid))
		util_panic("Unable to put first thread in vfs");

	return t;
}

size_t thread_getCount(void) {
	return sll_length(threads);
}

sThread *thread_getRunning(void) {
	return cur;
}

void thread_setRunning(sThread *t) {
	cur = t;
}

sThread *thread_getById(tTid tid) {
	return tidToThread[tid];
}

void thread_switch(void) {
	thread_switchTo(sched_perform()->tid);
}

void thread_switchNoSigs(void) {
	/* remember that the current thread wants to ignore signals */
	cur->ignoreSignals = 1;
	thread_switch();
	cur->ignoreSignals = 0;
}

void thread_killDead(void) {
	/* destroy threads, if there are any */
	if(deadThreads != NULL) {
		sSLNode *n;
		sThread *t;
		for(n = sll_begin(deadThreads); n != NULL; n = n->next) {
			/* we want to destroy the stacks here because if the whole process is destroyed
			 * the proc-module doesn't kill the running thread anyway so there will never
			 * be the case that we should destroy a single thread later */
			t = (sThread*)n->data;
			thread_kill(t);
		}
		sll_destroy(deadThreads,false);
		deadThreads = NULL;
	}
}

bool thread_setReady(tTid tid) {
	sThread *t = thread_getById(tid);
	vassert(t != NULL && t != cur,"tid=%d, pid=%d, cmd=%s",
			t ? t->tid : 0,t ? t->proc->pid : 0,t ? t->proc->command : "?");
	if(!t->ignoreSignals) {
		sched_setReady(t);
		ev_removeThread(tid);
	}
	return t->state == ST_READY;
}

bool thread_setBlocked(tTid tid) {
	sThread *t = thread_getById(tid);
	assert(t != NULL);
	sched_setBlocked(t);
	return t->state == ST_BLOCKED;
}

void thread_setSuspended(tTid tid,bool blocked) {
	sThread *t = thread_getById(tid);
	assert(t != NULL);
	sched_setSuspended(t,blocked);
}

int thread_extendStack(uintptr_t address) {
	return vmm_growStackTo(cur,address);
}

int thread_clone(const sThread *src,sThread **dst,sProc *p,tFrameNo *stackFrame,bool cloneProc) {
	int err = ERR_NOT_ENOUGH_MEM;
	sThread *t = (sThread*)kheap_alloc(sizeof(sThread));
	if(t == NULL)
		return ERR_NOT_ENOUGH_MEM;

	t->tid = thread_getFreeTid();
	if(t->tid == INVALID_TID) {
		err = ERR_NO_FREE_THREADS;
		goto errThread;
	}

	t->state = ST_RUNNING;
	t->events = 0;
	t->ignoreSignals = 0;
	/* TODO find a nicer solution */
#ifdef __i386__
	fpu_cloneState(&(t->fpuState),src->fpuState);
#endif
	t->stats.kcycleCount.val64 = 0;
	t->stats.kcycleStart = 0;
	t->stats.ucycleCount.val64 = 0;
	t->stats.ucycleStart = 0;
	t->stats.schedCount = 0;
	t->stats.syscalls = 0;
	t->proc = p;
	if(cloneProc) {
		/* proc_clone() sets t->kstackFrame in this case */
		t->stackRegion = src->stackRegion;
		t->tlsRegion = src->tlsRegion;
	}
	else {
		/* no swapping here because we don't want to make a thread-switch */
		/* TODO */
		size_t neededFrms = 0;/*paging_countFramesForMap(
			t->ustackBegin - t->ustackPages * PAGE_SIZE,INITIAL_STACK_PAGES);*/
		if(pmem_getFreeFrames(MM_DEF) < 1 + neededFrms)
			goto errThread;

		/* add a new stack-region */
		t->stackRegion = vmm_add(p,NULL,0,INITIAL_STACK_PAGES * PAGE_SIZE,
				INITIAL_STACK_PAGES * PAGE_SIZE,REG_STACK);
		if(t->stackRegion < 0)
			goto errThread;
		/* add kernel-stack */
		*stackFrame = t->kstackFrame = pmem_allocate();
		p->ownFrames++;
		/* add a new tls-region, if its present in the src-thread */
		t->tlsRegion = -1;
		if(src->tlsRegion >= 0) {
			uintptr_t tlsStart,tlsEnd;
			vmm_getRegRange(src->proc,src->tlsRegion,&tlsStart,&tlsEnd);
			t->tlsRegion = vmm_add(p,NULL,0,tlsEnd - tlsStart,tlsEnd - tlsStart,REG_TLS);
			if(t->tlsRegion < 0)
				goto errStack;
		}
	}

	/* insert into thread-list */
	if(!thread_add(t))
		goto errClone;

	/* clone signal-handler (here because the thread needs to be in the map first) */
	if(cloneProc)
		sig_cloneHandler(src->tid,t->tid);

	/* insert in VFS; thread needs to be inserted for it */
	if(!vfs_createThread(t->tid))
		goto errAppend;

	*dst = t;
	return 0;

errAppend:
	thread_remove(t);
errClone:
	if(t->tlsRegion >= 0)
		vmm_remove(p,t->tlsRegion);
errStack:
	if(!cloneProc) {
		pmem_free(t->kstackFrame);
		p->ownFrames--;
		vmm_remove(p,t->stackRegion);
	}
errThread:
	kheap_free(t);
	return err;
}

void thread_kill(sThread *t) {
	/* we can't destroy the current thread */
	if(t == cur) {
		/* put it in the dead-thread-queue to destroy it later */
		if(deadThreads == NULL) {
			deadThreads = sll_create();
			if(deadThreads == NULL)
				util_panic("Not enough mem for dead thread-list");
		}
		if(!sll_append(deadThreads,t))
			util_panic("Not enough mem to append dead thread");
		/* remove from event-system */
		ev_removeThread(t->tid);
		/* remove from scheduler and ensure that he don't picks us again */
		sched_removeThread(t);
		/* remove from timer, too, so that we don't get waked up again */
		timer_removeThread(t->tid);
		t->state = ST_ZOMBIE;
		return;
	}

	/* remove tls */
	if(t->tlsRegion >= 0) {
		vmm_remove(t->proc,t->tlsRegion);
		t->tlsRegion = -1;
	}
	if(t->stackRegion >= 0) {
		/* remove stack-region */
		vmm_remove(t->proc,t->stackRegion);
		t->stackRegion = -1;
	}
	/* free kernel-stack */
	pmem_free(t->kstackFrame);
	t->proc->ownFrames--;

	/* remove from process */
	sll_removeFirst(t->proc->threads,t);
	/* if there is just one thread left we have to map his kernel-stack again because we won't
	 * do it for single-thread-processes on a switch for performance-reasons */
	if(sll_length(t->proc->threads) == 1) {
		tFrameNo stackFrame = ((sThread*)sll_get(t->proc->threads,0))->kstackFrame;
		paging_mapTo(t->proc->pagedir,KERNEL_STACK,&stackFrame,1,
				PG_PRESENT | PG_WRITABLE | PG_SUPERVISOR);
	}

	/* remove from all modules we may be announced */
	ev_removeThread(t->tid);
	sched_removeThread(t);
	timer_removeThread(t->tid);
	/* TODO find a nicer solution */
#ifdef __i386__
	fpu_freeState(&t->fpuState);
#endif
	vfs_removeThread(t->tid);

	/* notify the process about it */
	sig_addSignalFor(t->proc->pid,SIG_THREAD_DIED);
	ev_wakeup(EVI_THREAD_DIED,(tEvObj)t->proc);

	/* finally, destroy thread */
	thread_remove(t);
	kheap_free(t);
}

static tTid thread_getFreeTid(void) {
	size_t count = 0;
	while(count < MAX_THREAD_COUNT) {
		if(nextTid >= MAX_THREAD_COUNT)
			nextTid = 0;
		if(tidToThread[nextTid++] == NULL)
			return nextTid - 1;
		count++;
	}
	return INVALID_TID;
}

static bool thread_add(sThread *t) {
	if(!sll_append(threads,t))
		return false;
	tidToThread[t->tid] = t;
	return true;
}

static void thread_remove(sThread *t) {
	sll_removeFirst(threads,t);
	tidToThread[t->tid] = NULL;
}

/* #### TEST/DEBUG FUNCTIONS #### */
#if DEBUGGING

void thread_dbg_printAll(void) {
	sSLNode *n;
	vid_printf("Threads:\n");
	for(n = sll_begin(threads); n != NULL; n = n->next) {
		sThread *t = (sThread*)n->data;
		thread_dbg_print(t);
	}
}

void thread_dbg_print(const sThread *t) {
	sFuncCall *calls;
	static const char *states[] = {
		"UNUSED","RUNNING","READY","BLOCKED","ZOMBIE","BLOCKEDSWAP","READYSWAP"
	};
	vid_printf("\tThread %d: (process %d:%s)\n",t->tid,t->proc->pid,t->proc->command);
	vid_printf("\t\tState=%s\n",states[t->state]);
	vid_printf("\t\tEvents=");
	ev_dbg_printEvMask(t->events);
	vid_printf("\n");
	vid_printf("\t\tKstackFrame=%#x\n",t->kstackFrame);
	vid_printf("\t\tTlsRegion=%d, stackRegion=%d\n",t->tlsRegion,t->stackRegion);
	vid_printf("\t\tUCycleCount = %#016Lx\n",t->stats.ucycleCount.val64);
	vid_printf("\t\tKCycleCount = %#016Lx\n",t->stats.kcycleCount.val64);
	vid_printf("\t\tKernel-trace:\n");
	calls = util_getKernelStackTraceOf(t);
	while(calls->addr != 0) {
		vid_printf("\t\t\t%#08x -> %#08x (%s)\n",(calls + 1)->addr,calls->funcAddr,calls->funcName);
		calls++;
	}
	calls = util_getUserStackTraceOf(t);
	if(calls) {
		vid_printf("\t\tUser-trace:\n");
		while(calls->addr != 0) {
			vid_printf("\t\t\t%#08x -> %#08x (%s)\n",
					(calls + 1)->addr,calls->funcAddr,calls->funcName);
			calls++;
		}
	}
}

#endif