/* flasher for HM-MOD-UART
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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>
#include <poll.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/time.h>
#include <libusb-1.0/libusb.h>

#include "hexdump.h"
#include "firmware.h"
#include "version.h"
#include "hmuartlgw.h"

struct recv_data {
	uint16_t ack;
};

static int parse_hmuartlgw(enum hmuartlgw_dst dst, uint8_t *buf, int buf_len, void *data)
{
	struct recv_data *rdata = data;

	if (buf_len != 2)
		return 1;

	rdata->ack = (buf[0] << 8) | (buf[1] & 0xff);

	return 1;
}

void flash_hmmoduart_syntax(char *prog)
{
	fprintf(stderr, "Syntax: %s [options] -U /dev/ttyAMA0 filename.eq3\n\n", prog);
	fprintf(stderr, "Mandatory parameter:\n");
	fprintf(stderr, "\t-U device\tuse HM-MOD-UART on given device\n");
	fprintf(stderr, "\nOptional parameters:\n");
	fprintf(stderr, "\t-V\t\tshow version (" VERSION ")\n");

}

int main(int argc, char **argv)
{
	const char twiddlie[] = { '-', '\\', '|', '/' };
	struct hmuartlgw_dev *dev;
	uint8_t *framedata;
	struct recv_data rdata;
	uint16_t len;
	struct firmware *fw;
	char *uart = NULL;
	char *filename = NULL;
	int block;
	int pfd;
	int opt;
	int debug = 0;

	while((opt = getopt(argc, argv, "U:V")) != -1) {
		switch (opt) {
			case 'U':
				uart = optarg;
				break;
			case 'V':
				printf("flash-hmmoduart " VERSION "\n");
				printf("Copyright (c) 2016 Michael Gernoth\n\n");
				exit(EXIT_SUCCESS);
			case 'h':
			case ':':
			case '?':
			default:
				flash_hmmoduart_syntax(argv[0]);
				exit(EXIT_FAILURE);
				break;
		}
	}

	if (optind == argc - 1) {
		filename = argv[optind];
	}

	if (!uart) {
		flash_hmmoduart_syntax(argv[0]);
		exit(EXIT_FAILURE);
	}

	printf("HM-MOD-UART flasher version " VERSION "\n\n");

	if (!filename) {
		fprintf(stderr, "Missing firmware filename!\n\n");
		flash_hmmoduart_syntax(argv[0]);
		exit(EXIT_FAILURE);
	}

	fw = firmware_read_firmware(filename, debug);
	if (!fw)
		exit(EXIT_FAILURE);

	hmuartlgw_set_debug(debug);

	memset(&rdata, 0, sizeof(rdata));

	printf("\nInitializing HM-MOD-UART...\n");

	dev = hmuart_init(uart, parse_hmuartlgw, &rdata, 0);
	if (!dev) {
		fprintf(stderr, "Can't initialize HM-MOD-UART\n");
		exit(EXIT_FAILURE);
	}

	printf("HM-MOD-UART opened.\n\n");

	printf("Flashing %d blocks", fw->fw_blocks);
	if (debug) {
		printf("\n");
	} else {
		printf(": %c", twiddlie[0]);
		fflush(stdout);
	}

	for (block = 0; block < fw->fw_blocks; block++) {
		len = fw->fw[block][2] << 8;
		len |= fw->fw[block][3];

		len -= 1; /* + frametype, - crc crc */

		framedata = (fw->fw[block]) + 3;
		framedata[0] = HMUARTLGW_OS_UPDATE_FIRMWARE;

		if (debug)
			hexdump(framedata, len, "F> ");

		rdata.ack = 0;

		if (!hmuartlgw_send(dev, framedata, len, HMUARTLGW_OS)) {
			perror("\n\nhmuartlgw_send");
			exit(EXIT_FAILURE);
		}

		if (debug)
			printf("Waiting for ack...\n");
		do {
			errno = 0;
			pfd = hmuartlgw_poll(dev, 1000);
			if ((pfd < 0) && errno) {
				if (errno != ETIMEDOUT) {
					perror("\n\nhmuartlgw_poll");
					exit(EXIT_FAILURE);
				}
			}
			if (rdata.ack) {
				break;
			}
		} while (pfd < 0);

		if (rdata.ack != 0x0401) {
			fprintf(stderr, "\n\nError flashing block %d, status: %04x\n", block, rdata.ack);
			exit(EXIT_FAILURE);
		}

		if (!debug) {
			printf("\b%c", twiddlie[block % sizeof(twiddlie)]);
			fflush(stdout);
		}
	}

	if (rdata.ack == 0x0401) {
		printf("\n\nFirmware update successfull!\n");
	}


	firmware_free(fw);

	hmuartlgw_close(dev);

	return EXIT_SUCCESS;
}
