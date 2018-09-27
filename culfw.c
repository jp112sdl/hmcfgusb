/* culfw driver
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

#include "culfw.h"

struct culfw_dev *culfw_init(char *device, uint32_t speed, culfw_cb_fn cb, void *data)
{
	struct culfw_dev *dev = NULL;
	struct termios oldtio, tio;
	uint32_t brate;

	switch(speed) {
		case 115200:
			brate = B115200;
			break;
		case 57600:
			brate = B57600;
			break;
		case 38400:
			brate = B38400;
			break;
		case 19200:
			brate = B19200;
			break;
		case 9600:
			brate = B9600;
			break;
		default:
			fprintf(stderr, "Unsupported baud-rate: %u\n", speed);
			return NULL;
			break;
	}

	dev = malloc(sizeof(struct culfw_dev));
	if (dev == NULL) {
		perror("malloc(struct culfw_dev)");
		return NULL;
	}

	memset(dev, 0, sizeof(struct culfw_dev));

	dev->fd = open(device, O_RDWR | O_NOCTTY);
	if (dev->fd < 0) {
		perror("open(culfw)");
		goto out;
	}

	if (tcgetattr(dev->fd, &oldtio) == -1) {
		perror("tcgetattr");
		goto out2;
	}

	memset(&tio, 0, sizeof(tio));

	tio.c_cflag = brate | CS8 | CLOCAL | CREAD;
	tio.c_iflag = IGNPAR | ICRNL;
	tio.c_oflag = 0;
	tio.c_lflag = ICANON;
	tio.c_cc[VTIME] = 0;
	tio.c_cc[VMIN] = 1;

	tcflush(dev->fd, TCIFLUSH);
	if (tcsetattr(dev->fd, TCSANOW, &tio) == -1) {
		perror("tcsetattr");
		goto out2;
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

int culfw_send(struct culfw_dev *dev, char *cmd, int cmdlen)
{
	int w = 0;
	int ret;

	do {
		ret = write(dev->fd, cmd + w, cmdlen - w);
		if (ret < 0) {
			perror("write");
			return 0;
		}
		w += ret;
	} while (w < cmdlen);

	return 1;
}

int culfw_poll(struct culfw_dev *dev, int timeout)
{
	struct pollfd pfds[1];
	int ret;
	int r = 0;
	uint8_t buf[1024];

	errno = 0;

	memset(pfds, 0, sizeof(struct pollfd) * 1);

	pfds[0].fd = dev->fd;
	pfds[0].events = POLLIN;

	ret = poll(pfds, 1, timeout);
	if (ret == -1)
		return -1;

	if (ret == 0) {
		errno = ETIMEDOUT;
		return -1;
	}

	if (!(pfds[0].revents & POLLIN)) {
		errno = EIO;
		return -1;
	}

	memset(buf, 0, sizeof(buf));
	r = read(dev->fd, buf, sizeof(buf));
	if (r < 0)
		return -1;

	if (r == 0) {
		errno = EOF;
		return -1;
	}

	dev->cb(buf, r, dev->cb_data);

	errno = 0;
	return -1;
}

void culfw_close(struct culfw_dev *dev)
{
	close(dev->fd);
}

void culfw_flush(struct culfw_dev *dev)
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
