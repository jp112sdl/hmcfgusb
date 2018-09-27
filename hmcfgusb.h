/* HM-CFG-USB libusb-driver
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

typedef int (*hmcfgusb_cb_fn)(uint8_t *buf, int buf_len, void *data);

struct hmcfgusb_dev {
	libusb_device_handle *usb_devh;
	struct libusb_transfer *transfer;
	int n_usb_pfd;
	struct pollfd *pfd;
	int n_pfd;
	int bootloader;
	time_t opened_at;
};

int hmcfgusb_send(struct hmcfgusb_dev *usbdev, unsigned char* send_data, int len, int done);
int hmcfgusb_send_null_frame(struct hmcfgusb_dev *usbdev, int silent);
struct hmcfgusb_dev *hmcfgusb_init(hmcfgusb_cb_fn cb, void *data, char *serial);
int hmcfgusb_add_pfd(struct hmcfgusb_dev *dev, int fd, short events);
int hmcfgusb_poll(struct hmcfgusb_dev *dev, int timeout);
void hmcfgusb_enter_bootloader(struct hmcfgusb_dev *dev);
void hmcfgusb_leave_bootloader(struct hmcfgusb_dev *dev);
void hmcfgusb_close(struct hmcfgusb_dev *dev);
void hmcfgusb_exit(void);
void hmcfgusb_set_debug(int d);
