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

#include <common.h>
#include <timer.h>
#include <util.h>
#include <sched.h>
#include <kheap.h>
#include <sllist.h>
#include <errors.h>

#define TIMER_BASE_FREQUENCY	1193182
#define IOPORT_TIMER_CTRL		0x43
#define IOPORT_TIMER_CNTDIV		0x40

/* counter to select */
#define TIMER_CTRL_CNT0			0x00
#define TIMER_CTRL_CNT1			0x40
#define TIMER_CTRL_CNT2			0x80
/* read/write mode */
#define TIMER_CTRL_RWLO			0x10	/* low byte only */
#define TIMER_CTRL_RWHI			0x20	/* high byte only */
#define TIMER_CTRL_RWLOHI		0x30	/* low byte first, then high byte */
/* mode */
#define TIMER_CTRL_MODE1		0x02	/* programmable one shot */
#define TIMER_CTRL_MODE2		0x04	/* rate generator */
#define TIMER_CTRL_MODE3		0x06	/* square wave generator */
#define TIMER_CTRL_MODE4		0x08	/* software triggered strobe */
#define TIMER_CTRL_MODE5		0x0A	/* hardware triggered strobe */
/* count mode */
#define TIMER_CTRL_CNTBIN16		0x00	/* binary 16 bit */
#define TIMER_CTRL_CNTBCD		0x01	/* BCD */

/* an entry in the listener-list */
typedef struct {
	tPid pid;
	u64 time;
} sTimerListener;

/* processes that should be waked up to a specified time */
static sSLList *listener = NULL;
/* total elapsed milliseconds */
static u64 elapsedMsecs = 0;
static u64 lastResched = 0;

void timer_init(void) {
	/* change timer divisor */
	u16 freq = TIMER_BASE_FREQUENCY / TIMER_FREQUENCY;
	util_outByte(IOPORT_TIMER_CTRL,TIMER_CTRL_CNT0 | TIMER_CTRL_RWLOHI |
			TIMER_CTRL_MODE2 | TIMER_CTRL_CNTBIN16);
	util_outByte(IOPORT_TIMER_CNTDIV,freq & 0xFF);
	util_outByte(IOPORT_TIMER_CNTDIV,freq >> 8);

	/* init list */
	listener = sll_create();
	if(listener == NULL)
		util_panic("Not enough mem for timer-listener");
}

s32 timer_sleepFor(tPid pid,u32 msecs) {
	sTimerListener *l = (sTimerListener*)kheap_alloc(sizeof(sTimerListener));
	if(l == 0)
		return ERR_NOT_ENOUGH_MEM;

	/* build entry and put process to sleep */
	l->pid = pid;
	l->time = elapsedMsecs + msecs;
	if(!sll_append(listener,l)) {
		kheap_free(l);
		return ERR_NOT_ENOUGH_MEM;
	}

	sched_setBlocked(proc_getByPid(pid));
	return 0;
}

void timer_intrpt(void) {
	bool foundProc = false;
	sSLNode *n,*p,*tn;
	sTimerListener *l;

	elapsedMsecs += 1000 / TIMER_FREQUENCY;

	/* TODO we should switch to the process for which (elapsedMsecs - l->time) is max */
	/* look if there is a process that should be waked up */
	p = NULL;
	for(n = sll_begin(listener); n != NULL; ) {
		l = (sTimerListener*)n->data;
		if(l->time <= elapsedMsecs) {
			tn = n->next;
			foundProc = true;
			/* wake up process */
			sched_setReadyQuick(proc_getByPid(l->pid));
			kheap_free(l);
			sll_removeNode(listener,n,p);
			n = tn;
		}
		else {
			p = n;
			n = n->next;
		}
	}

	/* if a process has been waked up or the time-slice is over, reschedule */
	if(foundProc || (elapsedMsecs - lastResched) >= PROC_TIMESLICE) {
		lastResched = elapsedMsecs;
		proc_switch();
	}
}
