;
; $Id: startup.s 602 2010-04-07 19:18:57Z nasmussen $
; Copyright (C) 2008 - 2009 Nils Asmussen
;
; This program is free software; you can redistribute it and/or
; modify it under the terms of the GNU General Public License
; as published by the Free Software Foundation; either version 2
; of the License, or (at your option) any later version.
;
; This program is distributed in the hope that it will be useful,
; but WITHOUT ANY WARRANTY; without even the implied warranty of
; MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
; GNU General Public License for more details.
;
; You should have received a copy of the GNU General Public License
; along with this program; if not, write to the Free Software
; Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
;

[BITS 32]

[global init]
[global lookupSymbolStart]
[extern lookupSymbol]
[extern loadProg]

ALIGN 4

;  Initial stack:
;  +------------------+  <- top
;  |     arguments    |
;  |        ...       |
;  +------------------+
;  |       argv       |
;  +------------------+
;  |       argc       |
;  +------------------+
;  |     TLSSize      |  0 if not present
;  +------------------+
;  |     TLSStart     |  0 if not present
;  +------------------+
;  |    entryPoint    |  0 for initial thread, thread-entrypoint for others
;  +------------------+
;  |    fd for prog   |
;  +------------------+

init:
	; we have no TLS, so don't waste time
	call	loadProg
	; first, remove fd from stack
	add		esp,4
	; loadProg returns the entrypoint
	jmp		eax

lookupSymbolStart:
	call	lookupSymbol
	add		esp,8
	; jump to function
	jmp		eax
