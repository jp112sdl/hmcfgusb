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

#define DEFAULT_CUL_BPS	38400

typedef int (*culfw_cb_fn)(uint8_t *buf, int buf_len, void *data);

struct culfw_dev {
	int fd;
	culfw_cb_fn cb;
	void *cb_data;
};

struct culfw_dev *culfw_init(char *device, uint32_t speed, culfw_cb_fn cb, void *data);
int culfw_send(struct culfw_dev *dev, char *cmd, int cmdlen);
int culfw_poll(struct culfw_dev *dev, int timeout);
void culfw_close(struct culfw_dev *dev);
void culfw_flush(struct culfw_dev *dev);
