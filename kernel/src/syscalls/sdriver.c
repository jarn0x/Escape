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
#include <mem/paging.h>
#include <task/thread.h>
#include <task/signals.h>
#include <vfs/rw.h>
#include <syscalls/driver.h>
#include <syscalls.h>
#include <errors.h>

/* implementable functions */
#define DRV_ALL						(DRV_OPEN | DRV_READ | DRV_WRITE | DRV_CLOSE | DRV_TERM)

#define GW_NOBLOCK					1

void sysc_regDriver(sIntrptStackFrame *stack) {
	const char *name = (const char*)SYSC_ARG1(stack);
	u32 flags = SYSC_ARG2(stack);
	sThread *t = thread_getRunning();
	tDrvId res;

	/* check flags */
	if((flags & ~DRV_ALL) != 0 && flags != DRV_FS)
		SYSC_ERROR(stack,ERR_INVALID_ARGS);
	if(!sysc_isStringReadable(name))
		SYSC_ERROR(stack,ERR_INVALID_ARGS);

	res = vfs_createDriver(t->tid,name,flags);
	if(res < 0)
		SYSC_ERROR(stack,res);
	SYSC_RET1(stack,res);
}

void sysc_unregDriver(sIntrptStackFrame *stack) {
	tDrvId id = SYSC_ARG1(stack);
	sThread *t = thread_getRunning();
	s32 err;

	/* check node-number */
	if(!vfsn_isValidNodeNo(id))
		SYSC_ERROR(stack,ERR_INVALID_ARGS);

	/* remove the driver */
	err = vfs_removeDriver(t->tid,id);
	if(err < 0)
		SYSC_ERROR(stack,err);
	SYSC_RET1(stack,0);
}

void sysc_setDataReadable(sIntrptStackFrame *stack) {
	tDrvId id = SYSC_ARG1(stack);
	bool readable = (bool)SYSC_ARG2(stack);
	sThread *t = thread_getRunning();
	s32 err;

	/* check node-number */
	if(!vfsn_isValidNodeNo(id))
		SYSC_ERROR(stack,ERR_INVALID_ARGS);

	err = vfs_setDataReadable(t->tid,id,readable);
	if(err < 0)
		SYSC_ERROR(stack,err);
	SYSC_RET1(stack,0);
}

void sysc_getClientThread(sIntrptStackFrame *stack) {
	tDrvId id = (tDrvId)SYSC_ARG1(stack);
	tTid tid = (tPid)SYSC_ARG2(stack);
	sThread *t = thread_getRunning();
	tFD fd;
	tFileNo file;
	s32 res;

	if(thread_getById(tid) == NULL)
		SYSC_ERROR(stack,ERR_INVALID_TID);

	/* we need a file-desc */
	fd = thread_getFreeFd();
	if(fd < 0)
		SYSC_ERROR(stack,fd);

	/* open client */
	file = vfs_openClientThread(t->tid,id,tid);
	if(file < 0)
		SYSC_ERROR(stack,file);

	/* associate fd with file */
	res = thread_assocFd(fd,file);
	if(res < 0) {
		/* we have already opened the file */
		vfs_closeFile(t->tid,file);
		SYSC_ERROR(stack,res);
	}

	SYSC_RET1(stack,fd);
}

void sysc_getWork(sIntrptStackFrame *stack) {
	tDrvId *ids = (tDrvId*)SYSC_ARG1(stack);
	u32 idCount = SYSC_ARG2(stack);
	tDrvId *drv = (tDrvId*)SYSC_ARG3(stack);
	tMsgId *id = (tMsgId*)SYSC_ARG4(stack);
	u8 *data = (u8*)SYSC_ARG5(stack);
	u32 size = SYSC_ARG6(stack);
	u8 flags = (u8)SYSC_ARG7(stack);
	sThread *t = thread_getRunning();
	tFileNo file;
	tFD fd;
	sVFSNode *cnode;
	tInodeNo client;
	s32 res;

	/* validate driver-ids */
	if(idCount <= 0 || idCount > 32 || ids == NULL ||
			!paging_isRangeUserReadable((u32)ids,idCount * sizeof(tDrvId)))
		SYSC_ERROR(stack,ERR_INVALID_ARGS);
	/* validate id and data */
	if(!paging_isRangeUserWritable((u32)id,sizeof(tMsgId)) ||
			!paging_isRangeUserWritable((u32)data,size))
		SYSC_ERROR(stack,ERR_INVALID_ARGS);
	/* check drv */
	if(drv != NULL && !paging_isRangeUserWritable((u32)drv,sizeof(tDrvId)))
		SYSC_ERROR(stack,ERR_INVALID_ARGS);

	/* open a client */
	while(1) {
		client = vfs_getClient(t->tid,(tInodeNo*)ids,idCount);
		if(client != ERR_NO_CLIENT_WAITING)
			break;

		/* if we shouldn't block, stop here */
		if(flags & GW_NOBLOCK)
			SYSC_ERROR(stack,client);

		/* otherwise wait for a client (accept signals) */
		thread_wait(t->tid,0,EV_CLIENT);
		thread_switch();
		if(sig_hasSignalFor(t->tid))
			SYSC_ERROR(stack,ERR_INTERRUPTED);
	}
	if(client < 0)
		SYSC_ERROR(stack,client);

	/* get fd for communication with the client */
	fd = thread_getFreeFd();
	if(fd < 0)
		SYSC_ERROR(stack,fd);

	/* open file */
	file = vfs_openFile(t->tid,VFS_READ | VFS_WRITE,client,VFS_DEV_NO);
	if(file < 0)
		SYSC_ERROR(stack,file);

	/* assoc with fd */
	res = thread_assocFd(fd,file);
	if(res < 0) {
		vfs_closeFile(t->tid,file);
		SYSC_ERROR(stack,res);
	}

	/* receive a message */
	cnode = vfsn_getNode(client);
	res = vfsrw_readDrvUse(t->tid,file,cnode,id,data,size);
	if(res < 0) {
		thread_unassocFd(fd);
		vfs_closeFile(t->tid,file);
		SYSC_ERROR(stack,res);
	}

	if(drv)
		*drv = NADDR_TO_VNNO(cnode->parent);
	SYSC_RET1(stack,fd);
}