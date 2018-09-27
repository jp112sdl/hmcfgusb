/* HM-MOD-UART/HM-LGW-O-TW-W-EU driver
 *
 * Copyright (c) 2016-17 Michael Gernoth <michael@gernoth.net>
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

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <poll.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

#include "hexdump.h"
#include "hmuartlgw.h"

#define HMUARTLGW_INIT_TIMEOUT	10000

#define HMUARTLGW_SETTLE_TIME	1

static int debug = 0;

enum hmuartlgw_state {
	HMUARTLGW_QUERY_APPSTATE,
	HMUARTLGW_ENTER_BOOTLOADER,
	HMUARTLGW_ENTER_BOOTLOADER_ACK,
	HMUARTLGW_BOOTLOADER,
	HMUARTLGW_HMIP_BOOTLOADER,
	HMUARTLGW_ENTER_APPLICATION,
	HMUARTLGW_ENTER_APPLICATION_ACK,
	HMUARTLGW_APPLICATION,
	HMUARTLGW_DUAL_APPLICATION,
	HMUARTLGW_HMIP_APPLICATION,
};

struct recv_data {
	enum hmuartlgw_state state;
	struct hmuartlgw_dev *dev;
};


#define CRC16_POLY	0x8005

static uint16_t crc16(uint8_t* buf, int length)
{
	uint16_t crc = 0xd77f;
	int i;

	while (length--) {
		crc ^= *buf++ << 8;
		for (i = 0; i < 8; i++) {
			if (crc & 0x8000) {
				crc <<= 1;
				crc ^= CRC16_POLY;
			} else {
				crc <<= 1;
			}
		}
	}

	return crc;
}

static int hmuartlgw_init_parse(enum hmuartlgw_dst dst, uint8_t *buf, int buf_len, void *data)
{
	struct recv_data *rdata = data;

#if 0
	if (debug) {
		printf("Length: %d\n", buf_len);
		hexdump(buf, buf_len, "INIT > ");
	}
#endif

	/* Minimally handle DualCopro-Firmware */
	if (dst == HMUARTLGW_DUAL) {
		if ((buf_len == 14) && (buf[0] == 0x00) && !strncmp(((char*)buf)+1, "DualCoPro_App", 13)) {
			rdata->state = HMUARTLGW_DUAL_APPLICATION;
			return 1;
		}

		switch (rdata->state) {
			case HMUARTLGW_QUERY_APPSTATE:
				if ((buf[0] == 0x05) && (buf[1] == 0x01)) {
					if (!strncmp(((char*)buf)+2, "DualCoPro_App", 13)) {
						rdata->state = HMUARTLGW_DUAL_APPLICATION;
						return 1;
					} else if (!strncmp(((char*)buf)+2, "HMIP_TRX_App", 12)) {
						rdata->state = HMUARTLGW_HMIP_APPLICATION;
						return 1;
					} else if (!strncmp(((char*)buf)+2, "HMIP_TRX_Bl", 11)) {
						rdata->state = HMUARTLGW_HMIP_BOOTLOADER;
						return 1;
					}
				}
				break;
			case HMUARTLGW_ENTER_BOOTLOADER:
				if ((buf_len == 2) &&
				    (buf[0] == 0x05) &&
				    (buf[1] == 0x01)) {
					rdata->state = HMUARTLGW_ENTER_BOOTLOADER_ACK;
					return 1;
				}
				break;
			default:
				fprintf(stderr, "Don't know how to handle this state (%d) for unsupported firmware, giving up!\n", rdata->state);
				exit(1);
				break;
		}

		return 0;
	}

	/* Re-send commands for DualCopro Firmware */
	if (dst == HMUARTLGW_DUAL_ERR) {
		uint8_t buf[128] = { 0 };

		switch(rdata->state) {
			case HMUARTLGW_QUERY_APPSTATE:
				if (debug) {
					printf("Re-sending appstate-query for new firmare\n");
				}

				buf[0] = HMUARTLGW_DUAL_GET_APP;
				hmuartlgw_send(rdata->dev, buf, 1, HMUARTLGW_DUAL);
				break;
			case HMUARTLGW_ENTER_BOOTLOADER:
				if (debug) {
					printf("Re-sending switch to bootloader for new firmare\n");
				}

				buf[0] = HMUARTLGW_DUAL_CHANGE_APP;
				hmuartlgw_send(rdata->dev, buf, 1, HMUARTLGW_DUAL);
				break;
			default:
				fprintf(stderr, "Don't know how to handle this error-state (%d) for unsupported firmware, giving up!\n", rdata->state);
				exit(1);
				break;
		}

		return 0;
	}

	if (dst != HMUARTLGW_OS)
		return 0;

	if ((buf_len == 10) && (buf[0] == 0x00) && !strncmp(((char*)buf)+1, "Co_CPU_BL", 9)) {
		rdata->state = HMUARTLGW_BOOTLOADER;
		return 1;
	}

	if ((buf_len == 11) && (buf[0] == 0x00) && !strncmp(((char*)buf)+1, "Co_CPU_App", 10)) {
		rdata->state = HMUARTLGW_APPLICATION;
		return 1;
	}

	switch(rdata->state) {
		case HMUARTLGW_QUERY_APPSTATE:
			if ((buf[0] == HMUARTLGW_OS_ACK) && (buf[1] == 0x02)) {
				if (!strncmp(((char*)buf)+2, "Co_CPU_BL", 9)) {
					rdata->state = HMUARTLGW_BOOTLOADER;
				} else if (!strncmp(((char*)buf)+2, "Co_CPU_App", 10)) {
					rdata->state = HMUARTLGW_APPLICATION;
				}
			}
			break;
		case HMUARTLGW_ENTER_BOOTLOADER:
			if ((buf_len == 2) &&
			    (buf[0] == HMUARTLGW_OS_ACK) &&
			    (buf[1] == 0x01)) {
				rdata->state = HMUARTLGW_ENTER_BOOTLOADER_ACK;
			}
			break;
		case HMUARTLGW_ENTER_BOOTLOADER_ACK:
			rdata->state = HMUARTLGW_ENTER_BOOTLOADER;
			break;
		case HMUARTLGW_ENTER_APPLICATION:
			if ((buf_len == 2) &&
			    (buf[0] == HMUARTLGW_OS_ACK) &&
			    (buf[1] == 0x01)) {
				rdata->state = HMUARTLGW_ENTER_APPLICATION_ACK;
			}
			break;
		case HMUARTLGW_ENTER_APPLICATION_ACK:
			rdata->state = HMUARTLGW_ENTER_APPLICATION;
			break;
		default:
			return 0;
			break;
	}

	/* Try to query current app in case we might be in the DUAL/HMIP-Bootloader */
	if ((buf[0] == HMUARTLGW_OS_ACK) && (buf[1] == 0x03)) {
		buf[0] = HMUARTLGW_DUAL_GET_APP;
		hmuartlgw_send(rdata->dev, buf, 1, HMUARTLGW_DUAL);
	}

        return 1;
}

struct hmuartlgw_dev *hmuart_init(char *device, hmuartlgw_cb_fn cb, void *data, int app)
{
	struct hmuartlgw_dev *dev = NULL;
	struct termios oldtio, tio;

	dev = malloc(sizeof(struct hmuartlgw_dev));
	if (dev == NULL) {
		perror("malloc(struct hmuartlgw_dev)");
		return NULL;
	}

	memset(dev, 0, sizeof(struct hmuartlgw_dev));

	dev->fd = open(device, O_RDWR | O_NOCTTY);
	if (dev->fd < 0) {
		perror("open(hmuartlgw)");
		goto out;
	}

	if (debug) {
		fprintf(stderr, "%s opened\n", device);
	}

	if (tcgetattr(dev->fd, &oldtio) == -1) {
		perror("tcgetattr");
		goto out2;
	}

	memset(&tio, 0, sizeof(tio));

	tio.c_cflag = CS8 | CLOCAL | CREAD;
	tio.c_iflag = IGNPAR;
	tio.c_oflag = 0;
	tio.c_lflag = 0;
	tio.c_cc[VTIME] = 0;
	tio.c_cc[VMIN] = 1;
        cfsetspeed(&tio, B115200);

	tcflush(dev->fd, TCIFLUSH);
	if (tcsetattr(dev->fd, TCSANOW, &tio) == -1) {
		perror("tcsetattr");
		goto out2;
	}

	if (debug) {
		fprintf(stderr, "serial parameters set\n");
	}

	hmuartlgw_flush(dev);

	if (app) {
		hmuartlgw_enter_app(dev);
	} else {
		hmuartlgw_enter_bootloader(dev);
	}

	dev->cb = cb;
	dev->cb_data = data;

	return dev;

out2:
	close(dev->fd);
out:
	free(dev);
	return NULL;
}

struct hmuartlgw_dev *hmlgw_init(char *device, hmuartlgw_cb_fn cb, void *data)
{
	struct hmuartlgw_dev *dev = NULL;

	return dev;
}

void hmuartlgw_enter_bootloader(struct hmuartlgw_dev *dev)
{
	hmuartlgw_cb_fn cb_old = dev->cb;
	void *cb_data_old = dev->cb_data;
	struct recv_data rdata = { 0 };
	uint8_t buf[128] = { 0 };
	int ret;

	if (debug) {
		fprintf(stderr, "Entering bootloader\n");
	}

	dev->cb = hmuartlgw_init_parse;
	dev->cb_data = &rdata;

	rdata.dev = dev;
	rdata.state = HMUARTLGW_QUERY_APPSTATE;
	buf[0] = HMUARTLGW_OS_GET_APP;
	hmuartlgw_send(dev, buf, 1, HMUARTLGW_OS);
	do {
		errno = 0;
		ret = hmuartlgw_poll(dev, HMUARTLGW_INIT_TIMEOUT);
		if (ret == -1 && errno == ETIMEDOUT) {
			fprintf(stderr, "Communication with the module timed out, is the serial port configured correctly?\n");
			exit(1);
		}
	} while (rdata.state == HMUARTLGW_QUERY_APPSTATE);

	if ((rdata.state != HMUARTLGW_BOOTLOADER) &&
	    (rdata.state != HMUARTLGW_HMIP_BOOTLOADER)) {
		rdata.dev = dev;
		rdata.state = HMUARTLGW_ENTER_BOOTLOADER;
		buf[0] = HMUARTLGW_OS_CHANGE_APP;
		hmuartlgw_send(dev, buf, 1, HMUARTLGW_OS);
		do {
			errno = 0;
			ret = hmuartlgw_poll(dev, HMUARTLGW_INIT_TIMEOUT);
			if (ret == -1 && errno == ETIMEDOUT) {
				fprintf(stderr, "Communication with the module timed out, is the serial port configured correctly?\n");
				exit(1);
			}
		} while ((rdata.state != HMUARTLGW_BOOTLOADER) &&
		         (rdata.state != HMUARTLGW_HMIP_BOOTLOADER));

		printf("Waiting for bootloader to settle...\n");
		sleep(HMUARTLGW_SETTLE_TIME);
	}

	dev->cb = cb_old;
	dev->cb_data = cb_data_old;
}

void hmuartlgw_enter_app(struct hmuartlgw_dev *dev)
{
	hmuartlgw_cb_fn cb_old = dev->cb;
	void *cb_data_old = dev->cb_data;
	struct recv_data rdata = { 0 };
	uint8_t buf[128] = { 0 };
	int ret;

	if (debug) {
		fprintf(stderr, "Entering application\n");
	}

	dev->cb = hmuartlgw_init_parse;
	dev->cb_data = &rdata;

	rdata.dev = dev;
	rdata.state = HMUARTLGW_QUERY_APPSTATE;
	buf[0] = HMUARTLGW_OS_GET_APP;
	hmuartlgw_send(dev, buf, 1, HMUARTLGW_OS);
	do {
		errno = 0;
		ret = hmuartlgw_poll(dev, HMUARTLGW_INIT_TIMEOUT);
		if (ret == -1 && errno == ETIMEDOUT) {
			fprintf(stderr, "Communication with the module timed out, is the serial port configured correctly?\n");
			exit(1);
		}
	} while (rdata.state == HMUARTLGW_QUERY_APPSTATE);

	if ((rdata.state != HMUARTLGW_APPLICATION) &&
	    (rdata.state != HMUARTLGW_DUAL_APPLICATION)) {
		rdata.dev = dev;
		rdata.state = HMUARTLGW_ENTER_APPLICATION;
		buf[0] = HMUARTLGW_OS_CHANGE_APP;
		hmuartlgw_send(dev, buf, 1, HMUARTLGW_OS);
		do {
			errno = 0;
			ret = hmuartlgw_poll(dev, HMUARTLGW_INIT_TIMEOUT);
			if (ret == -1 && errno == ETIMEDOUT) {
				fprintf(stderr, "Communication with the module timed out, is the serial port configured correctly?\n");
				exit(1);
			}
		} while ((rdata.state != HMUARTLGW_APPLICATION) &&
		         (rdata.state != HMUARTLGW_DUAL_APPLICATION));

		if (rdata.state == HMUARTLGW_APPLICATION) {
			printf("Waiting for application to settle...\n");
			sleep(HMUARTLGW_SETTLE_TIME);
		}
	}

	if (rdata.state == HMUARTLGW_DUAL_APPLICATION) {
		fprintf(stderr, "Unsupported firmware, please install HM-only firmware!\n");
		exit(1);
	}


	dev->cb = cb_old;
	dev->cb_data = cb_data_old;
}

static int hmuartlgw_escape(uint8_t *frame, int framelen)
{
	int i;

	for (i = 1; i < framelen; i++) {
		if (frame[i] == 0xfc || frame[i] == 0xfd) {
			memmove(frame + i + 1, frame + i, framelen - i);
			frame[i++] = 0xfc;
			frame[i] &= 0x7f;
			framelen++;
		}
	}
	return framelen;
}

int hmuartlgw_send_raw(struct hmuartlgw_dev *dev, uint8_t *frame, int framelen)
{
	int w = 0;
	int ret;

	if (debug) {
		hexdump(frame, framelen, "UARTLGW < ");
	}

	framelen = hmuartlgw_escape(frame, framelen);

	do {
		ret = write(dev->fd, frame + w, framelen - w);
		if (ret < 0) {
			perror("write");
			return 0;
		}
		w += ret;
	} while (w < framelen);

	return 1;
}

int hmuartlgw_send(struct hmuartlgw_dev *dev, uint8_t *cmd, int cmdlen, enum hmuartlgw_dst dst)
{
	static uint8_t cnt = 0;
	uint8_t frame[4096] = { 0 };
	uint16_t crc;

	frame[0] = 0xfd;
	frame[1] = ((cmdlen + 2) >> 8) & 0xff;
	frame[2] = (cmdlen + 2) & 0xff;
	frame[3] = dst;
	dev->last_send_cnt = cnt;
	frame[4] = cnt++;
	memcpy(&(frame[5]), cmd, cmdlen);
	crc = crc16(frame, cmdlen + 5);
	frame[cmdlen + 5] = (crc >> 8) & 0xff;
	frame[cmdlen + 6] = crc & 0xff;

	return hmuartlgw_send_raw(dev, frame, cmdlen + 7);
}

int hmuartlgw_poll(struct hmuartlgw_dev *dev, int timeout)
{
	struct pollfd pfds[1];
	int ret;
	int r = 0;
	uint16_t crc;

	errno = 0;

	memset(pfds, 0, sizeof(struct pollfd) * 1);

	pfds[0].fd = dev->fd;
	pfds[0].events = POLLIN;

	ret = poll(pfds, 1, timeout);
	if (ret == -1)
		return -1;

	errno = 0;
	if (ret == 0) {
		errno = ETIMEDOUT;
		return -1;
	}

	if (!(pfds[0].revents & POLLIN)) {
		errno = EIO;
		return -1;
	}

	r = read(dev->fd, dev->buf+dev->pos, 1);
	if (r < 0)
		return -1;

	if (r == 0) {
		errno = EOF;
		return -1;
	}

	dev->pos += r;

	if (dev->buf[0] != 0xfd) {
		memset(dev->buf, 0, sizeof(dev->buf));
		dev->pos = 0;
		dev->unescape_next = 0;
		return -1;
	}

	if (dev->unescape_next) {
		dev->buf[dev->pos-1] |= 0x80;
		dev->unescape_next = 0;
	} else if (dev->buf[dev->pos-1] == 0xfc) {
		dev->unescape_next = 1;
		dev->pos--;
		return -1;
	}

	if (dev->pos >= 3) {
		uint16_t len;

		len = ((dev->buf[1] << 8) & 0xff00) | (dev->buf[2] & 0xff);

		if (dev->pos < len + 5)
			return -1;
	} else {
		return -1;
	}

	crc = crc16(dev->buf, dev->pos);
	if (crc == 0x0000) {
		if (debug)
			hexdump(dev->buf, dev->pos, "UARTLGW > ");

		dev->cb(dev->buf[3], dev->buf + 5 , dev->pos - 7, dev->cb_data);

		memset(dev->buf, 0, sizeof(dev->buf));
		dev->pos = 0;
		dev->unescape_next = 0;
	} else {
		fprintf(stderr, "Invalid checksum received!\n");
		hexdump(dev->buf, dev->pos, "ERR> ");
		printf("calculated: %04x\n", crc);

		memset(dev->buf, 0, sizeof(dev->buf));
		dev->pos = 0;
		dev->unescape_next = 0;
	}

	errno = 0;
	return -1;
}

void hmuartlgw_close(struct hmuartlgw_dev *dev)
{
	close(dev->fd);
}

void hmuartlgw_flush(struct hmuartlgw_dev *dev)
{
	struct pollfd pfds[1];
	int ret;
	int r = 0;
	uint8_t buf[1024];

	tcflush(dev->fd, TCIOFLUSH);

	while(1) {
		memset(pfds, 0, sizeof(struct pollfd) * 1);

		pfds[0].fd = dev->fd;
		pfds[0].events = POLLIN;

		ret = poll(pfds, 1, 100);
		if (ret <= 0)
			break;

		if (!(pfds[0].revents & POLLIN))
			break;

		memset(buf, 0, sizeof(buf));
		r = read(dev->fd, buf, sizeof(buf));
		if (r <= 0)
			break;
	}

	return;
}

void hmuartlgw_set_debug(int d)
{
	debug = d;
}
