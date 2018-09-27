/* HM-sniffer for HM-CFG-USB
 *
 * Copyright (c) 2013-16 Michael Gernoth <michael@gernoth.net>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>
#include <poll.h>
#include <errno.h>
#include <time.h>
#include <sys/time.h>
#include <libusb-1.0/libusb.h>

#include "version.h"
#include "hexdump.h"
#include "hmcfgusb.h"
#include "hmuartlgw.h"
#include "hm.h"

static int verbose = 0;

/* See HMConfig.pm */
char *hm_message_types(uint8_t type, uint8_t subtype)
{
	switch(type) {
		case 0x00:
			return "Device Info";
			break;
		case 0x01:
			return "Configuration";
			break;
		case 0x02:
			if (subtype >= 0x80 && subtype <= 0x8f) {
				return "NACK";
			} else if (subtype == 0x01) {
				return "ACKinfo";
			} else if (subtype == 0x04) {
				return "AESrequest";
			}
			return "ACK";
			break;
		case 0x03:
			return "AESreply";
			break;
		case 0x04:
			return "AESkey";
			break;
		case 0x10:
			return "Information";
			break;
		case 0x11:
			return "SET";
			break;
		case 0x12:
			return "HAVE_DATA";
			break;
		case 0x3e:
			return "Switch";
			break;
		case 0x3f:
			return "Timestamp";
			break;
		case 0x40:
			return "Remote";
			break;
		case 0x41:
			return "Sensor";
			break;
		case 0x53:
			return "Water sensor";
			break;
		case 0x54:
			return "Gas sensor";
			break;
		case 0x58:
			return "Climate event";
			break;
		case 0x5a:
			return "Thermal control";
			break;
		case 0x5e:
		case 0x5f:
			return "Power event";
			break;
		case 0x70:
			return "Weather event";
			break;
		case 0xca:
			return "Firmware";
			break;
		case 0xcb:
			return "Rf configuration";
			break;
		default:
			return "?";
			break;
	}
}

static void dissect_hm(uint8_t *buf, int len)
{
	struct timeval tv;
	struct tm *tmp;
	char ts[32];
	static int count = 0;
	int i;

	gettimeofday(&tv, NULL);
	tmp = localtime(&tv.tv_sec);
	memset(ts, 0, sizeof(ts));
	strftime(ts, sizeof(ts)-1, "%Y-%m-%d %H:%M:%S", tmp);

	if (verbose) {
		printf("%s.%06ld: ", ts, tv.tv_usec);

		for (i = 0; i < len; i++) {
			printf("%02X", buf[i]);
		}
		printf("\n");
		printf("Packet information:\n");
		printf("\tLength: %u\n", buf[0]);
		printf("\tMessage ID: %u\n", buf[1]);
		printf("\tSender: %02x%02x%02x\n", buf[4], buf[5], buf[6]);
		printf("\tReceiver: %02x%02x%02x\n", buf[7], buf[8], buf[9]);
		printf("\tControl Byte: 0x%02x\n", buf[2]);
		printf("\t\tFlags: ");
		if (buf[2] & (1 << 0)) printf("WAKEUP ");
		if (buf[2] & (1 << 1)) printf("WAKEMEUP ");
		if (buf[2] & (1 << 2)) printf("CFG ");
		if (buf[2] & (1 << 3)) printf("? ");
		if (buf[2] & (1 << 4)) printf("BURST ");
		if (buf[2] & (1 << 5)) printf("BIDI ");
		if (buf[2] & (1 << 6)) printf("RPTED ");
		if (buf[2] & (1 << 7)) printf("RPTEN ");
		printf("\n");
		printf("\tMessage type: %s (0x%02x 0x%02x)\n", hm_message_types(buf[3], buf[10]), buf[3], buf[10]);
		printf("\tMessage: ");
		for (i = 10; i < len; i++) {
			printf("%02X", buf[i]);
		}
		printf("\n");

		printf("\n");
	} else {
		if (!(count++ % 20))
			printf("                         LL NR FL CM sender recvr  payload\n");

		printf("%s.%03ld: %02X %02X %02X %02X %02X%02X%02X %02X%02X%02X ",
				ts, tv.tv_usec/1000,
				buf[0], buf[1], buf[2], buf[3],
				buf[4], buf[5], buf[6],
				buf[7], buf[8], buf[9]);

		for (i = 10; i < len; i++) {
			printf("%02X", buf[i]);
		}
		printf("%s(%s)\n", (i>10)?" ":"", hm_message_types(buf[3], buf[10]));
	}
}

struct recv_data {
	int wrong_hmid;
};

static int parse_hmcfgusb(uint8_t *buf, int buf_len, void *data)
{
	struct recv_data *rdata = data;

	if (buf_len < 1)
		return 1;

	switch(buf[0]) {
		case 'E':
			dissect_hm(buf + 13, buf[13] + 1);
			break;
		case 'H':
			if ((buf[27] != 0x00) ||
			    (buf[28] != 0x00) ||
			    (buf[29] != 0x00)) {
				printf("hmId is currently set to: %02x%02x%02x\n", buf[27], buf[28], buf[29]);
				rdata->wrong_hmid = 1;
			}
			break;
		case 'R':
		case 'I':
		case 'G':
			break;
		default:
			hexdump(buf, buf_len, "Unknown> ");
			break;
	}

	return 1;
}

static int parse_hmuartlgw(enum hmuartlgw_dst dst, uint8_t *buf, int buf_len, void *data)
{
	if (dst == HMUARTLGW_OS) {
		if ((buf[0] != HMUARTLGW_OS_ACK) ||
		    (buf[1] != 0x01)) {
			hexdump(buf, buf_len, "OS> ");
		}
		return 0;
	}

	if (dst != HMUARTLGW_APP) {
		return 0;
	}

	switch(buf[0]) {
		case HMUARTLGW_APP_RECV:
			buf[3] = buf_len - 4;
			dissect_hm(buf + 3, buf_len - 3);
		case HMUARTLGW_APP_ACK:
			break;
		default:
			hexdump(buf, buf_len, "Unknown> ");
	}

	return 1;
}

void hmsniff_syntax(char *prog)
{
	fprintf(stderr, "Syntax: %s options\n\n", prog);
	fprintf(stderr, "Possible options:\n");
	fprintf(stderr, "\t-f\t\tfast (100k/firmware update) mode\n");
	fprintf(stderr, "\t-S serial\tuse HM-CFG-USB with given serial\n");
	fprintf(stderr, "\t-U device\tuse HM-MOD-UART on given device\n");
	fprintf(stderr, "\t-v\t\tverbose mode\n");
	fprintf(stderr, "\t-V\t\tshow version (" VERSION ")\n");

}

int main(int argc, char **argv)
{
	struct hm_dev dev = { 0 };
	struct recv_data rdata;
	char *serial = NULL;
	char *uart = NULL;
	int quit = 0;
	int speed = 10;
	uint8_t buf[32];
	int opt;

	dev.type = DEVICE_TYPE_HMCFGUSB;

	while((opt = getopt(argc, argv, "fS:U:vV")) != -1) {
		switch (opt) {
			case 'f':
				speed = 100;
				break;
			case 'S':
				serial = optarg;
				break;
			case 'U':
				uart = optarg;
				dev.type = DEVICE_TYPE_HMUARTLGW;
				break;
			case 'v':
				verbose = 1;
				break;
			case 'V':
				printf("hmsniff " VERSION "\n");
				printf("Copyright (c) 2013-16 Michael Gernoth\n\n");
				exit(EXIT_SUCCESS);
			case 'h':
			case ':':
			case '?':
			default:
				hmsniff_syntax(argv[0]);
				exit(EXIT_FAILURE);
				break;
		}
	}

	if (dev.type == DEVICE_TYPE_HMCFGUSB) {
		hmcfgusb_set_debug(0);
	} else {
		hmuartlgw_set_debug(0);
	}

	do {
		memset(&rdata, 0, sizeof(rdata));
		rdata.wrong_hmid = 0;

		if (dev.type == DEVICE_TYPE_HMCFGUSB) {
			dev.hmcfgusb = hmcfgusb_init(parse_hmcfgusb, &rdata, serial);
			if (!dev.hmcfgusb) {
				fprintf(stderr, "Can't initialize HM-CFG-USB, retrying in 1s...\n");
				sleep(1);
				continue;
			}
			printf("HM-CFG-USB opened!\n");

			hmcfgusb_send_null_frame(dev.hmcfgusb, 1);
			hmcfgusb_send(dev.hmcfgusb, (unsigned char*)"K", 1, 1);

			hmcfgusb_send_null_frame(dev.hmcfgusb, 1);
			buf[0] = 'G';
			buf[1] = speed;
			hmcfgusb_send(dev.hmcfgusb, buf, 2, 1);
		} else {
			dev.hmuartlgw = hmuart_init(uart, parse_hmuartlgw, &rdata, 1);
			if (!dev.hmuartlgw) {
				fprintf(stderr, "Can't initialize HM-MOD-UART!\n");
				exit(1);
			}
			printf("HM-MOD-UART opened!\n");

			buf[0] = HMUARTLGW_APP_SET_HMID;
			buf[1] = 0x00;
			buf[2] = 0x00;
			buf[3] = 0x00;
			hmuartlgw_send(dev.hmuartlgw, buf, 4, HMUARTLGW_APP);
			do { hmuartlgw_poll(dev.hmuartlgw, 500); } while (errno != ETIMEDOUT);
			if (speed == 100) {
				buf[0] = HMUARTLGW_OS_UPDATE_MODE;
				buf[1] = 0xe9;
				buf[2] = 0xca;
				hmuartlgw_send(dev.hmuartlgw, buf, 3, HMUARTLGW_OS);
			} else {
				buf[0] = HMUARTLGW_OS_NORMAL_MODE;
				hmuartlgw_send(dev.hmuartlgw, buf, 1, HMUARTLGW_OS);
			}
		}

		while(!quit) {
			int fd;

			if (rdata.wrong_hmid) {
				printf("changing hmId to 000000, this might reboot the device!\n");
				if (dev.type == DEVICE_TYPE_HMCFGUSB) {
					hmcfgusb_send(dev.hmcfgusb, (unsigned char*)"A\00\00\00", 4, 1);
					rdata.wrong_hmid = 0;
					hmcfgusb_send(dev.hmcfgusb, (unsigned char*)"K", 1, 1);
				} else {
					buf[0] = HMUARTLGW_APP_SET_HMID;
					buf[1] = 0x00;
					buf[2] = 0x00;
					buf[3] = 0x00;
					hmuartlgw_send(dev.hmuartlgw, buf, 4, HMUARTLGW_APP);
				}
			}
			if (dev.type == DEVICE_TYPE_HMCFGUSB) {
				fd = hmcfgusb_poll(dev.hmcfgusb, 1000);
			} else {
				fd = hmuartlgw_poll(dev.hmuartlgw, 60000);
			}
			if (fd >= 0) {
				fprintf(stderr, "activity on unknown fd %d!\n", fd);
				continue;
			} else if (fd == -1) {
				if (errno) {
					if (errno != ETIMEDOUT) {
						perror("hmsniff_poll");
						break;
					} else {
						/* periodically wakeup the device */
						if (dev.type == DEVICE_TYPE_HMCFGUSB) {
							hmcfgusb_send_null_frame(dev.hmcfgusb, 1);
						}
					}
				}
			}
		}

		if (dev.type == DEVICE_TYPE_HMCFGUSB) {
			if (dev.hmcfgusb)
				hmcfgusb_close(dev.hmcfgusb);
		} else {
			if (dev.hmuartlgw)
				hmuartlgw_close(dev.hmuartlgw);
		}
	} while (!quit);

	if (dev.type == DEVICE_TYPE_HMCFGUSB) {
		hmcfgusb_exit();
	}

	return EXIT_SUCCESS;
}
