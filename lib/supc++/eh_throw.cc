// -*- C++ -*- Exception handling routines for throwing.
// Copyright (C) 2001, 2002, 2003, 2004, 2005, 2006, 2007, 2008, 2009
// Free Software Foundation, Inc.
//
// This file is part of GCC.
//
// GCC is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 3, or (at your option)
// any later version.
//
// GCC is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// Under Section 7 of GPL version 3, you are granted additional
// permissions described in the GCC Runtime Library Exception, version
// 3.1, as published by the Free Software Foundation.

// You should have received a copy of the GNU General Public License and
// a copy of the GCC Runtime Library Exception along with this program;
// see the files COPYING3 and COPYING.RUNTIME respectively.  If not, see
// <http://www.gnu.org/licenses/>.

#include "unwind-cxx.h"
#include <esc/debug.h>

using namespace __cxxabiv1;

static void __gxx_exception_cleanup(_Unwind_Reason_Code code,_Unwind_Exception *exc) {
	// This cleanup is set only for primaries.
	__cxa_refcounted_exception *header = __get_refcounted_exception_header_from_ue(exc);

	// We only want to be called through _Unwind_DeleteException.
	// _Unwind_DeleteException in the HP-UX IA64 libunwind library
	// returns _URC_NO_REASON and not _URC_FOREIGN_EXCEPTION_CAUGHT
	// like the GCC _Unwind_DeleteException function does.
	if(code != _URC_FOREIGN_EXCEPTION_CAUGHT && code != _URC_NO_REASON)
		__terminate(header->exc.terminateHandler);

#ifdef _GLIBCXX_ATOMIC_BUILTINS_4
	if(__sync_sub_and_fetch(&header->referenceCount,1) == 0) {
#endif
		if(header->exc.exceptionDestructor)
			header->exc.exceptionDestructor(header + 1);

		__cxa_free_exception(header + 1);
#ifdef _GLIBCXX_ATOMIC_BUILTINS_4
	}
#endif
}

extern "C" void __cxxabiv1::__cxa_throw(void *obj,std::type_info *tinfo,void(*dest)(void *)) {
	// Definitely a primary.
	__cxa_refcounted_exception *header = __get_refcounted_exception_header_from_obj(obj);
	header->referenceCount = 1;
	header->exc.exceptionType = tinfo;
	header->exc.exceptionDestructor = dest;
	header->exc.unexpectedHandler = __unexpected_handler;
	header->exc.terminateHandler = __terminate_handler;
	__GXX_INIT_PRIMARY_EXCEPTION_CLASS(header->exc.unwindHeader.exception_class);
	header->exc.unwindHeader.exception_cleanup = __gxx_exception_cleanup;

	_Unwind_RaiseException(&header->exc.unwindHeader);

	// Some sort of unwinding error.  Note that terminate is a handler.
	__cxa_begin_catch(&header->exc.unwindHeader);
	std::terminate();
}

extern "C" void __cxxabiv1::__cxa_rethrow() {
	__cxa_eh_globals *globals = __cxa_get_globals();
	__cxa_exception *header = globals->caughtExceptions;

	globals->uncaughtExceptions += 1;

	// Watch for luser rethrowing with no active exception.
	if(header) {
		// Tell __cxa_end_catch this is a rethrow.
		if(!__is_gxx_exception_class(header->unwindHeader.exception_class))
			globals->caughtExceptions = 0;
		else
			header->handlerCount = -header->handlerCount;

		_Unwind_Resume_or_Rethrow(&header->unwindHeader);

		// Some sort of unwinding error.  Note that terminate is a handler.
		__cxa_begin_catch(&header->unwindHeader);
	}
	std::terminate();
}