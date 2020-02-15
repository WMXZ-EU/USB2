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

#if F_CPU >= 20000000

#include "usb2_dev.h"
#include "usb2_seremu.h"
//#include "HardwareSerial.h"
#include <string.h> // for memcpy()

#ifdef SEREMU_INTERFACE // defined by usb2_dev.h -> usb2_desc.h

void log_write(char *txt, uint32_t val);

//volatile uint8_t usb2_seremu_transmit_flush_timer=0;

//static usb_packet_t *rx_packet=NULL;
//static usb_packet_t *tx_packet=NULL;
static volatile uint8_t tx_noautoflush=0;

#define TRANSMIT_FLUSH_TIMEOUT	5   /* in milliseconds */

static void timer_config(void (*callback)(void), uint32_t microseconds);
static void timer_start_oneshot();
static void timer_stop();


#define TX_NUM   7
#define TX_SIZE  (4*SEREMU_TX_SIZE)  /* should be a multiple of SEREMU_TX_SIZE */
static transfer_t tx_transfer[TX_NUM] __attribute__ ((used, aligned(32)));
static uint8_t txbuffer[TX_SIZE * TX_NUM];
static uint8_t tx_head=0;
static uint16_t tx_available=0;

#define RX_NUM  3
static transfer_t rx_transfer[RX_NUM] __attribute__ ((used, aligned(32)));
static uint8_t rx_buffer[RX_NUM * SEREMU_RX_SIZE];
static uint16_t rx_count[RX_NUM];
static uint16_t rx_index[RX_NUM];


static void rx_event(transfer_t *t)
{
	int len = SEREMU_RX_SIZE - ((t->status >> 16) & 0x7FFF);
	int index = t->callback_param;
	//printf("rx event, len=%d, i=%d\n", len, index);
	log_write("rx_event",len);
	rx_count[index] = len;
	rx_index[index] = 0;
}

void usb2_seremu_reset(void)
{
//	printf("usb2_serial_reset\n");
	// deallocate all transfer descriptors
}

void usb2_seremu_configure(void)
{
//	printf("usb2_serial_configure\n");
	log_write("seremu_config",sizeof(tx_transfer));
	memset(tx_transfer, 0, sizeof(tx_transfer));
	tx_head = 0;
	tx_available = 0;
	memset(rx_transfer, 0, sizeof(rx_transfer));
	memset(rx_count, 0, sizeof(rx_count));
	memset(rx_index, 0, sizeof(rx_index));
	usb2_config_rx(SEREMU_RX_ENDPOINT, SEREMU_RX_SIZE, 0, rx_event);
	usb2_config_tx(SEREMU_TX_ENDPOINT, SEREMU_TX_SIZE, 0, NULL);
	usb2_prepare_transfer(rx_transfer + 0, rx_buffer + 0, SEREMU_RX_SIZE, 0);
	usb2_receive(SEREMU_RX_ENDPOINT, rx_transfer + 0);
	timer_config(usb2_seremu_flush_callback, TRANSMIT_FLUSH_TIMEOUT);
}

// get the next character, or -1 if nothing received
int usb2_seremu_getchar(void)
{
	if (rx_index[0] < rx_count[0]) {
		int c = rx_buffer[rx_index[0]++];
		if (rx_index[0] >= rx_count[0]) {
			// reschedule transfer
			usb2_prepare_transfer(rx_transfer + 0, rx_buffer + 0, SEREMU_RX_SIZE, 0);
			usb2_receive(SEREMU_RX_ENDPOINT, rx_transfer + 0);
		}
		return c;
	}
	return -1;
}

// peek at the next character, or -1 if nothing received
int usb2_seremu_peekchar(void)
{
	if (rx_index[0] < rx_count[0]) {
		return rx_buffer[rx_index[0]];
	}

	return -1;
}

// number of bytes available in the receive buffer
int usb2_seremu_available(void)
{
	return rx_count[0] - rx_index[0];
}


// discard any buffered input
void usb2_seremu_flush_input(void)
{
	if (rx_index[0] < rx_count[0]) {
		rx_index[0] = rx_count[0];
		usb2_prepare_transfer(rx_transfer + 0, rx_buffer + 0, SEREMU_RX_SIZE, 0);
		usb2_receive(SEREMU_RX_ENDPOINT, rx_transfer + 0);
	}
}



// Maximum number of transmit packets to queue so we don't starve other endpoints for memory
#define TX_PACKET_LIMIT 6

// When the PC isn't listening, how long do we wait before discarding data?  If this is
// too short, we risk losing data during the stalls that are common with ordinary desktop
// software.  If it's too long, we stall the user's program when no software is running.
#define TX_TIMEOUT_MSEC 30
#if F_CPU == 256000000
  #define TX_TIMEOUT (TX_TIMEOUT_MSEC * 1706)
#elif F_CPU == 240000000
  #define TX_TIMEOUT (TX_TIMEOUT_MSEC * 1600)
#elif F_CPU == 216000000
  #define TX_TIMEOUT (TX_TIMEOUT_MSEC * 1440)
#elif F_CPU == 192000000
  #define TX_TIMEOUT (TX_TIMEOUT_MSEC * 1280)
#elif F_CPU == 180000000
  #define TX_TIMEOUT (TX_TIMEOUT_MSEC * 1200)
#elif F_CPU == 168000000
  #define TX_TIMEOUT (TX_TIMEOUT_MSEC * 1100)
#elif F_CPU == 144000000
  #define TX_TIMEOUT (TX_TIMEOUT_MSEC * 932)
#elif F_CPU == 120000000
  #define TX_TIMEOUT (TX_TIMEOUT_MSEC * 764)
#elif F_CPU == 96000000
  #define TX_TIMEOUT (TX_TIMEOUT_MSEC * 596)
#elif F_CPU == 72000000
  #define TX_TIMEOUT (TX_TIMEOUT_MSEC * 512)
#elif F_CPU == 48000000
  #define TX_TIMEOUT (TX_TIMEOUT_MSEC * 428)
#elif F_CPU == 24000000
  #define TX_TIMEOUT (TX_TIMEOUT_MSEC * 262)
#endif


// When we've suffered the transmit timeout, don't wait again until the computer
// begins accepting data.  If no software is running to receive, we'll just discard
// data as rapidly as Serial.print() can generate it, until there's something to
// actually receive it.
static uint8_t transmit_previous_timeout=0;

// transmit a character.  0 returned on success, -1 on error
int usb2_seremu_putchar(uint8_t c)
{
	return usb2_seremu_write(&c, 1);
}

extern volatile uint32_t systick_millis_count;

static void timer_config(void (*callback)(void), uint32_t microseconds)
{
	usb2_timer0_callback = callback;
	USB2_GPTIMER0CTRL = 0;
	USB2_GPTIMER0LD = microseconds - 1;
	USB2_USBINTR |= USB_USBINTR_TIE0;
}

static void timer_start_oneshot(void)
{
	// restarts timer if already running (retriggerable one-shot)
	USB2_GPTIMER0CTRL = USB_GPTIMERCTRL_GPTRUN | USB_GPTIMERCTRL_GPTRST;
}

static void timer_stop(void)
{
	USB2_GPTIMER0CTRL = 0;
}

int usb2_seremu_write(const void *buffer, uint32_t size)
{
	uint32_t sent=0;
	const uint8_t *data = (const uint8_t *)buffer;

//	log_write("usb2_conf ", usb2_configuration);
	if (!usb2_configuration) return 0;
	log_write("tx_size",size);
	while (size > 0) {
		transfer_t *xfer = tx_transfer + tx_head;
		int waiting=0;
		uint32_t wait_begin_at=0;
		while (!tx_available) {
			//digitalWriteFast(3, HIGH);
			uint32_t status = usb2_transfer_status(xfer);
	log_write("tx_stat  ", status);
			if (!(status & 0x80)) {
				if (status & 0x68) {
					// TODO: what if status has errors???
//					printf("ERROR status = %x, i=%d, ms=%u\n",
//						status, tx_head, systick_millis_count);
				}
				tx_available = TX_SIZE;
				transmit_previous_timeout = 0;
				break;
			}
			if (!waiting) {
				wait_begin_at = systick_millis_count;
				waiting = 1;
			}
	log_write("tx_tout  ", transmit_previous_timeout);
			if (transmit_previous_timeout) return sent;
			if (systick_millis_count - wait_begin_at > TX_TIMEOUT_MSEC) {
				// waited too long, assume the USB host isn't listening
				transmit_previous_timeout = 1;
				return sent;
				//printf("\nstop, waited too long\n");
				//printf("status = %x\n", status);
				//printf("tx head=%d\n", tx_head);
				//printf("TXFILLTUNING=%08lX\n", USB1_TXFILLTUNING);
				//usb2_print_transfer_log();
				//while (1) ;
			}
//	log_write("tx_conf  ", usb2_configuration);
			if (!usb2_configuration) return sent;
			yield();
		}
	log_write("tx_avail2  ", tx_available);
	log_write("tx_size2  ", size);
		//digitalWriteFast(3, LOW);
		uint8_t *txdata = txbuffer + (tx_head * TX_SIZE) + (TX_SIZE - tx_available);
		if (size >= tx_available) {
			memcpy(txdata, data, tx_available);
			//*(txbuffer + (tx_head * TX_SIZE)) = 'A' + tx_head; // to see which buffer
			//*(txbuffer + (tx_head * TX_SIZE) + 1) = ' '; // really see it
			usb2_prepare_transfer(xfer, txbuffer + (tx_head * TX_SIZE), TX_SIZE, 0);
			usb2_transmit(SEREMU_TX_ENDPOINT, xfer);
			if (++tx_head >= TX_NUM) tx_head = 0;
			size -= tx_available;
			sent += tx_available;
			data += tx_available;
			tx_available = 0;
			timer_stop();
		} else {
			memcpy(txdata, data, size);
			tx_available -= size;
			sent += size;
	log_write("tx_avail3  ", tx_available);
	log_write("tx_size3  ", size);
			size = 0;
			timer_start_oneshot();
		}
	}
//	log_write("tx_start",sent);
	return sent;
/*	
#if 0
	uint32_t len;
	uint32_t wait_count;
	const uint8_t *src = (const uint8_t *)buffer;
	uint8_t *dest;

	tx_noautoflush = 1;
	while (size > 0) {
		if (!tx_packet) {
			wait_count = 0;
			while (1) {
				if (!usb_configuration) {
					tx_noautoflush = 0;
					return -1;
				}
				if (usb_tx_packet_count(SEREMU_TX_ENDPOINT) < TX_PACKET_LIMIT) {
					tx_noautoflush = 1;
					tx_packet = usb_malloc();
					if (tx_packet) break;
				}
				if (++wait_count > TX_TIMEOUT || transmit_previous_timeout) {
					transmit_previous_timeout = 1;
					tx_noautoflush = 0;
					return -1;
				}
				tx_noautoflush = 0;
				yield();
				tx_noautoflush = 1;
			}
		}
		transmit_previous_timeout = 0;
		len = SEREMU_TX_SIZE - tx_packet->index;
		if (len > size) len = size;
		dest = tx_packet->buf + tx_packet->index;
		tx_packet->index += len;
		size -= len;
		while (len-- > 0) *dest++ = *src++;
		if (tx_packet->index < SEREMU_TX_SIZE) {
			usb_seremu_transmit_flush_timer = TRANSMIT_FLUSH_TIMEOUT;
		} else {
			tx_packet->len = SEREMU_TX_SIZE;
			usb_seremu_transmit_flush_timer = 0;
			usb_tx(SEREMU_TX_ENDPOINT, tx_packet);
			tx_packet = NULL;
		}
	}
	tx_noautoflush = 0;
	return 0;
#endif
*/
}

int usb2_seremu_write_buffer_free(void)
{
	uint32_t sum = 0;
	tx_noautoflush = 1;
	for (uint32_t i=0; i < TX_NUM; i++) {
		if (i == tx_head) continue;
		if (!(usb2_transfer_status(tx_transfer + i) & 0x80)) sum += TX_SIZE;
	}
	tx_noautoflush = 0;
	return sum;
}

void usb2_seremu_flush_output(void)
{
	if (!usb2_configuration) return;
	if (tx_available == 0) return;

	log_write("  OUT tx_available",tx_available);	
	tx_noautoflush = 1;
	transfer_t *xfer = tx_transfer + tx_head;
	usb2_prepare_transfer(xfer, txbuffer + (tx_head * TX_SIZE), TX_SIZE - tx_available, 0);
	usb2_transmit(SEREMU_TX_ENDPOINT, xfer);
	if (++tx_head >= TX_NUM) tx_head = 0;
	tx_available = 0;
	tx_noautoflush = 0;

}

void usb2_seremu_flush_callback(void)
{
	if (tx_noautoflush) return;
	if (!usb2_configuration) return;
	if (tx_available == 0) return;

	log_write("  CB tx_available",tx_available);
	transfer_t *xfer = tx_transfer + tx_head;
	usb2_prepare_transfer(xfer, txbuffer + (tx_head * TX_SIZE), TX_SIZE - tx_available, 0);
	usb2_transmit(SEREMU_TX_ENDPOINT, xfer);
	if (++tx_head >= TX_NUM) tx_head = 0;
	log_write("  CB tx_head",tx_head);
	tx_available = 0;
}

#endif // SEREMU_INTERFACE

#endif // F_CPU >= 20 MHz
