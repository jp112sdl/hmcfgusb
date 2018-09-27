/* generic firmware-functions for HomeMatic
 *
 * Copyright (c) 2014-16 Michael Gernoth <michael@gernoth.net>
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
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/time.h>

#include "util.h"
#include "firmware.h"

/* This might be wrong, but it works for current fw */
#define MAX_BLOCK_LENGTH	2048

struct firmware* firmware_read_firmware(char *filename, int debug)
{
	struct firmware *fw;
	struct stat stat_buf;
	uint8_t buf[4096];
	uint16_t len;
	int fd;
	int r;
	int i;

	fw = malloc(sizeof(struct firmware));
	if (!fw) {
		perror("malloc(fw)");
		return NULL;
	}

	memset(fw, 0, sizeof(struct firmware));

	if (stat(filename, &stat_buf) == -1) {
		fprintf(stderr, "Can't stat %s: %s\n", filename, strerror(errno));
		exit(EXIT_FAILURE);
	}

	fd = open(filename, O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "Can't open %s: %s", filename, strerror(errno));
		exit(EXIT_FAILURE);
	}

	printf("Reading firmware from %s...\n", filename);
	do {
		memset(buf, 0, sizeof(buf));
		r = read(fd, buf, 4);
		if (r < 0) {
			perror("read");
			exit(EXIT_FAILURE);
		} else if (r == 0) {
			break;
		} else if (r != 4) {
			printf("can't get length information!\n");
			exit(EXIT_FAILURE);
		}

		for (i = 0; i < r; i++) {
			if (!validate_nibble(buf[i])) {
				fprintf(stderr, "Firmware file not valid!\n");
				exit(EXIT_FAILURE);
			}
		}

		len = (ascii_to_nibble(buf[0]) & 0xf)<< 4;
		len |= ascii_to_nibble(buf[1]) & 0xf;
		len <<= 8;
		len |= (ascii_to_nibble(buf[2]) & 0xf)<< 4;
		len |= ascii_to_nibble(buf[3]) & 0xf;

		if (len > MAX_BLOCK_LENGTH) {
			fprintf(stderr, "Invalid block-length %u > %u for block %d!\n", len, MAX_BLOCK_LENGTH, fw->fw_blocks+1);
			exit(EXIT_FAILURE);
		}

		fw->fw = realloc(fw->fw, sizeof(uint8_t*) * (fw->fw_blocks + 1));
		if (fw->fw == NULL) {
			perror("Can't reallocate fw->fw-blocklist");
			exit(EXIT_FAILURE);
		}

		fw->fw[fw->fw_blocks] = malloc(len + 4);
		if (fw->fw[fw->fw_blocks] == NULL) {
			perror("Can't allocate memory for fw->fw-block");
			exit(EXIT_FAILURE);
		}

		fw->fw[fw->fw_blocks][0] = (fw->fw_blocks >> 8) & 0xff;
		fw->fw[fw->fw_blocks][1] = fw->fw_blocks & 0xff;
		fw->fw[fw->fw_blocks][2] = (len >> 8) & 0xff;
		fw->fw[fw->fw_blocks][3] = len & 0xff;

		r = read(fd, buf, len * 2);
		if (r < 0) {
			perror("read");
			exit(EXIT_FAILURE);
		} else if (r < len * 2) {
			fprintf(stderr, "short read, aborting (%d < %d)\n", r, len * 2);
			exit(EXIT_FAILURE);
		}

		for (i = 0; i < r; i+=2) {
			if ((!validate_nibble(buf[i])) ||
			    (!validate_nibble(buf[i+1]))) {
				fprintf(stderr, "Firmware file not valid!\n");
				exit(EXIT_FAILURE);
			}

			fw->fw[fw->fw_blocks][(i/2) + 4] = (ascii_to_nibble(buf[i]) & 0xf)<< 4;
			fw->fw[fw->fw_blocks][(i/2) + 4] |= ascii_to_nibble(buf[i+1]) & 0xf;
		}

		fw->fw_blocks++;
		if (debug)
			printf("Firmware block %d with length %u read.\n", fw->fw_blocks, len);
	} while(r > 0);

	if (fw->fw_blocks == 0) {
		fprintf(stderr, "Firmware file not valid!\n");
		exit(EXIT_FAILURE);
	}

	printf("Firmware with %d blocks successfully read.\n", fw->fw_blocks);

	return fw;
}

void firmware_free(struct firmware *fw)
{
	int i;

	for (i = 0; i < fw->fw_blocks; i++)
		free(fw->fw[i]);

	free(fw->fw);
	free(fw);
}
