/* Teensyduino Core Library
 * http://www.pjrc.com/teensy/
 * Copyright (c) 2017 PJRC.COM, LLC.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * 1. The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * 2. If the Software is incorporated into a build system that allows
 * selection among a list of target devices, then similar target
 * devices manufactured by PJRC.COM must be included in the list of
 * target devices and selectable in the same manner.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
//WMXZ: cloned from rawhid and modified for mtp

#include "core_pins.h" // for yield(), millis()
#include <string.h>    // for memcpy()

#include "usb_dev.h"
#include "usb1_mtp.h"
//#include "HardwareSerial.h"

#define printf(...) //Serial.printf(__VA_ARGS__)

//#include "debug/printf.h"

#if defined(USB1_TEST) && defined(__IMXRT1062__) && defined(MTP_INTERFACE)

	#define TX_NUM   4
	static transfer_t tx_transfer[TX_NUM] __attribute__ ((used, aligned(32)));
	static uint8_t txbuffer[MTP_TX_SIZE * TX_NUM];
	static uint8_t tx_head=0;

	static transfer_t rx_transfer[1] __attribute__ ((used, aligned(32)));
	static uint8_t rx_buffer[MTP_RX_SIZE];

	extern volatile uint8_t usb_configuration;

    static uint32_t mtp_TXcount=0;
	static uint32_t mtp_RXcount=0;
	
	static void tx_event(transfer_t *t) {mtp_TXcount++;}
	static void rx_event(transfer_t *t) {mtp_RXcount++;}
	
	int usb_mtp_haveRX(void)
	{	static uint32_t old_RXcount=-1;
		if(mtp_RXcount==old_RXcount) return 0;
		old_RXcount++;
		return 1;
	}

	int usb_mtp_canTX(void)
	{	static uint32_t old_TXcount=0;
	    int can=mtp_TXcount - old_TXcount;
		old_TXcount++;
		return can<TX_NUM;
	}	

	void usb_mtp_configure(void)
	{
		printf("usb_mtp_configure\n");
		memset(tx_transfer, 0, sizeof(tx_transfer));
		memset(rx_transfer, 0, sizeof(rx_transfer));
		tx_head = 0;
		usb_config_tx(MTP_TX_ENDPOINT, MTP_TX_SIZE, 0, tx_event);
		usb_config_rx(MTP_RX_ENDPOINT, MTP_RX_SIZE, 0, rx_event);
		//usb_config_rx(MTP_RX_ENDPOINT, MTP_RX_SIZE, 0, NULL); // why does this not work?
		usb_prepare_transfer(rx_transfer + 0, rx_buffer, MTP_RX_SIZE, 0);
		usb_receive(MTP_RX_ENDPOINT, rx_transfer + 0);
	}

	static int usb_mtp_wait(transfer_t *xfer, uint32_t timeout)
	{
		uint32_t wait_begin_at = systick_millis_count;
		while (1) {
			if (!usb_configuration) return -1; // usb not enumerated by host
			uint32_t status = usb_transfer_status(xfer);
			if (!(status & 0x80)) break; // transfer descriptor ready
			if (systick_millis_count - wait_begin_at > timeout) return 0;
			yield();
		}
		return 1;
	}

	int usb_mtp_recv(void *buffer, uint32_t timeout)
	{
		int ret= usb_mtp_wait(rx_transfer, timeout); if(ret<=0) return ret;

		memcpy(buffer, rx_buffer, MTP_RX_SIZE);
		memset(rx_transfer, 0, sizeof(rx_transfer));
		usb_prepare_transfer(rx_transfer + 0, rx_buffer, MTP_RX_SIZE, 0);
		usb_receive(MTP_RX_ENDPOINT, rx_transfer + 0);
		return MTP_RX_SIZE;
	}

	int usb_mtp_send(const void *buffer,  int len, uint32_t timeout)
	{
		transfer_t *xfer = tx_transfer + tx_head;
		int ret= usb_mtp_wait(xfer, timeout); if(ret<=0) return ret;

		uint8_t *txdata = txbuffer + (tx_head * MTP_TX_SIZE);
		memcpy(txdata, buffer, len);
		usb_prepare_transfer(xfer, txdata,len, 0);
		usb_transmit(MTP_TX_ENDPOINT, xfer);
		if (++tx_head >= TX_NUM) tx_head = 0;
		asm("wfi");
		return len;
	}

	int usb_mtp_available(void)
	{
		if (!usb_configuration) return 0;
		if (!(usb_transfer_status(rx_transfer) & 0x80)) return MTP_RX_SIZE;
		return 0;
	}

#else
	void usb_mtp_configure(void) {}
#endif // MTP_INTERFACE
