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

#define HMUARTLGW_OS_GET_APP		0x00
#define HMUARTLGW_OS_GET_FIRMWARE	0x02
#define HMUARTLGW_OS_CHANGE_APP		0x03
#define HMUARTLGW_OS_ACK		0x04
#define HMUARTLGW_OS_UPDATE_FIRMWARE	0x05
#define HMUARTLGW_OS_UNSOL_CREDITS	0x05
#define HMUARTLGW_OS_NORMAL_MODE	0x06
#define HMUARTLGW_OS_UPDATE_MODE	0x07
#define HMUARTLGW_OS_GET_CREDITS	0x08
#define HMUARTLGW_OS_GET_SERIAL		0x0B
#define HMUARTLGW_OS_SET_TIME		0x0E

#define HMUARTLGW_APP_SET_HMID		0x00
#define HMUARTLGW_APP_GET_HMID		0x01
#define HMUARTLGW_APP_SEND		0x02
#define HMUARTLGW_APP_SET_CURRENT_KEY	0x03	/* key index */
#define HMUARTLGW_APP_ACK		0x04
#define HMUARTLGW_APP_RECV		0x05
#define HMUARTLGW_APP_ADD_PEER		0x06
#define HMUARTLGW_APP_REMOVE_PEER	0x07
#define HMUARTLGW_APP_GET_PEERS		0x08
#define HMUARTLGW_APP_PEER_ADD_AES	0x09
#define HMUARTLGW_APP_PEER_REMOVE_AES	0x0A
#define HMUARTLGW_APP_SET_OLD_KEY	0x0F	/* key index */
#define HMUARTLGW_APP_DEFAULT_HMID	0x10

#define HMUARTLGW_DUAL_GET_APP		0x01
#define HMUARTLGW_DUAL_CHANGE_APP	0x02

#define HMUARTLGW_ACK_EINPROGRESS	0x08

enum hmuartlgw_dst {
	HMUARTLGW_OS = 0,
	HMUARTLGW_APP = 1,
	HMUARTLGW_DUAL = 0xfe,
	HMUARTLGW_DUAL_ERR = 0xff,
};

typedef int (*hmuartlgw_cb_fn)(enum hmuartlgw_dst dst, uint8_t *buf, int buf_len, void *data);

struct hmuartlgw_dev {
	int fd;
	hmuartlgw_cb_fn cb;
	void *cb_data;
	uint8_t last_send_cnt;
	uint8_t buf[1024];
	int pos;
	int unescape_next;
};

struct hmuartlgw_dev *hmuart_init(char *device, hmuartlgw_cb_fn cb, void *data, int app);
struct hmuartlgw_dev *hmlgw_init(char *device, hmuartlgw_cb_fn cb, void *data);
int hmuartlgw_send_raw(struct hmuartlgw_dev *dev, uint8_t *frame, int framelen);
int hmuartlgw_send(struct hmuartlgw_dev *dev, uint8_t *cmd, int cmdlen, enum hmuartlgw_dst dst);
int hmuartlgw_poll(struct hmuartlgw_dev *dev, int timeout);
void hmuartlgw_close(struct hmuartlgw_dev *dev);
void hmuartlgw_flush(struct hmuartlgw_dev *dev);
void hmuartlgw_enter_bootloader(struct hmuartlgw_dev *dev);
void hmuartlgw_enter_app(struct hmuartlgw_dev *dev);
void hmuartlgw_set_debug(int d);
