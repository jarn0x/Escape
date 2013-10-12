/**
 * $Id$
 * Copyright (C) 2008 - 2011 Nils Asmussen
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

#include <esc/common.h>
#include <esc/arch/i586/ports.h>
#include <esc/driver.h>
#include <esc/io.h>
#include <esc/debug.h>
#include <esc/proc.h>
#include <esc/thread.h>
#include <esc/keycodes.h>
#include <esc/messages.h>
#include <keyb/keybdev.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <errno.h>

#include "set1.h"

/* io-ports */
#define IOPORT_PIC					0x20
#define IOPORT_KB_DATA				0x60
#define IOPORT_KB_CTRL				0x64

#define KBC_CMD_DISABLE_MOUSE		0xA7
#define KBC_CMD_ENABLE_MOUSE		0xA8

/* indicates that the output-buffer is full */
#define STATUS_OUTBUF_FULL			(1 << 0)
/* indicates that the input-buffer is full */
#define STATUS_INBUF_FULL			(1 << 1)
/* data for mouse */
#define STATUS_MOUSE_DATA_AVAIL		(1 << 5)

/* converts the scan-code to the ones IBM PCs expect */
#define CMD_IBM_COMPAT				(1 << 6)
/* performs no scan-code-conversion and no partity-check */
#define CMD_IBM_MODE				(1 << 5)
#define CMD_DISABLE_KEYBOARD		(1 << 4)
#define CMD_INHIBIT_OVERWRITE		(1 << 3)
/* bit-value is written to the system-flag in controller-status-register */
#define CMD_SYSTEM					(1 << 2)
/* raises an interrupt when the output-buffer is full */
#define CMD_EN_OUTBUF_INTRPT		(1 << 0)

/* ICW = initialisation command word */
#define PIC_ICW1					0x20

#define KB_MOUSE_LOCK				0x1

#define BUF_SIZE					128
#define SC_BUF_SIZE					16
#define TIMEOUT						60
#define SLEEP_TIME					20

static void kbIntrptHandler(int sig);
static void kb_waitOutBuf(void);
static void kb_waitInBuf(void);

int main(void) {
	uint8_t kbdata;

	/* request io-ports */
	if(reqport(IOPORT_PIC) < 0)
		error("Unable to request io-port %d",IOPORT_PIC);
	if(reqport(IOPORT_KB_DATA) < 0)
		error("Unable to request io-port",IOPORT_KB_DATA);
	if(reqport(IOPORT_KB_CTRL) < 0)
		error("Unable to request io-port",IOPORT_KB_CTRL);

	/* wait for input buffer empty */
	kb_waitInBuf();

	/* first read all bytes from the buffer; maybe there are scancodes... */
	while(inbyte(IOPORT_KB_CTRL) & STATUS_OUTBUF_FULL)
		inbyte(IOPORT_KB_DATA);

	/* self test */
	outbyte(IOPORT_KB_CTRL,0xAA);
	kb_waitOutBuf();
	kbdata = inbyte(IOPORT_KB_DATA);
	if(kbdata != 0x55)
		error("Keyboard-selftest failed: Got 0x%x, expected 0x55",kbdata);

	/* test interface */
	outbyte(IOPORT_KB_CTRL,0xAB);
	kb_waitOutBuf();
	kbdata = inbyte(IOPORT_KB_DATA);
	if(kbdata != 0x00)
		error("Interface-test failed: Got 0x%x, expected 0x00",kbdata);

	/* write command byte */
	outbyte(IOPORT_KB_CTRL,0x60);
	kb_waitInBuf();
	outbyte(IOPORT_KB_DATA,CMD_EN_OUTBUF_INTRPT | CMD_INHIBIT_OVERWRITE | CMD_IBM_COMPAT);
	kb_waitInBuf();

#if 0
	/* reset */
	outbyte(IOPORT_KB_DATA,0xFF);
	sleep(20);
	kb_waitOutBuf();
	kbdata = inbyte(IOPORT_KB_DATA);
	/* send echo-command */
	outbyte(IOPORT_KB_DATA,0xEE);
	sleep(20);
	kb_waitOutBuf();
	kbdata = inbyte(IOPORT_KB_DATA);
	sleep(20);
	if(kbdata != 0xEE)
		error("Keyboard-echo failed: Got 0x%x, expected 0xEE",kbdata);
#endif

	/* enable keyboard */
	outbyte(IOPORT_KB_DATA,0xF4);
	kb_waitOutBuf();
	/* clear output buffer */
	kbdata = inbyte(IOPORT_KB_DATA);

	/* disable LEDs
	kb_waitInBuf();
	outbyte(IOPORT_KB_DATA,0xED);
	kb_waitInBuf();
	outbyte(IOPORT_KB_DATA,0x0);*/

#if 0
	/* set scancode-set 1 */
	outbyte(IOPORT_KB_DATA,0xF0);
	outbyte(IOPORT_KB_DATA,0x1);
	sleep(20);
	kbdata = inbyte(IOPORT_KB_DATA);
	printf("Set scancode-set: %x\n",kbdata);
#endif

#if 0
	/* TODO doesn't work in qemu (but on real hardware). why? */
	/* set repeat-rate and delay */
	outbyte(IOPORT_KB_DATA,0xF3);
	outbyte(IOPORT_KB_DATA,0x24);	/*00100100*/
#endif

	/*outbyte(IOPORT_KB_DATA,0xF0);
	sleep(40);
	outbyte(IOPORT_KB_DATA,0x0);
	sleep(40);
	kbdata = inbyte(IOPORT_KB_DATA);
	printf("scancode-set=%x\n",kbdata);
	while(1);*/

	/* we want to get notified about keyboard interrupts */
	if(signal(SIG_INTRPT_KB,kbIntrptHandler) == SIG_ERR)
		error("Unable to announce sig-handler for %d",SIG_INTRPT_KB);

	keyb_driverLoop();

	/* clean up */
	relport(IOPORT_PIC);
	relport(IOPORT_KB_DATA);
	relport(IOPORT_KB_CTRL);
	return EXIT_SUCCESS;
}

static void kbIntrptHandler(A_UNUSED int sig) {
	if(!(inbyte(IOPORT_KB_CTRL) & STATUS_OUTBUF_FULL))
		return;

	uint8_t sc = inbyte(IOPORT_KB_DATA);
	sKbData data;
	if(kb_set1_getKeycode(&data.isBreak,&data.keycode,sc))
		keyb_broadcast(&data);
}

static void kb_waitOutBuf(void) {
	time_t elapsed = 0;
	uint8_t status;
	do {
		status = inbyte(IOPORT_KB_CTRL);
		if((status & STATUS_OUTBUF_FULL) == 0) {
			sleep(SLEEP_TIME);
			elapsed += SLEEP_TIME;
		}
	}
	while((status & STATUS_OUTBUF_FULL) == 0 && elapsed < TIMEOUT);
}

static void kb_waitInBuf(void) {
	time_t elapsed = 0;
	uint8_t status;
	do {
		status = inbyte(IOPORT_KB_CTRL);
		if((status & STATUS_INBUF_FULL) != 0) {
			sleep(SLEEP_TIME);
			elapsed += SLEEP_TIME;
		}
	}
	while((status & STATUS_INBUF_FULL) != 0 && elapsed < TIMEOUT);
}