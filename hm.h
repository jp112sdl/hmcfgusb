/* HomeMatic defines and functions
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

#define LEN	0x00
#define MSGID	0x01
#define CTL	0x02
#define TYPE	0x03
#define PAYLOAD	0x0a

#define SRC(buf)	(buf[0x06] | (buf[0x05] << 8) | (buf[0x04]) << 16)
#define DST(buf)	(buf[0x09] | (buf[0x08] << 8) | (buf[0x07]) << 16)

#define SET_SRC(buf, src)	do { buf[0x04] = (src >> 16) & 0xff; buf[0x05] = (src >> 8) & 0xff; buf[0x06] = src & 0xff; } while(0)
#define SET_DST(buf, dst)	do { buf[0x07] = (dst >> 16) & 0xff; buf[0x08] = (dst >> 8) & 0xff; buf[0x09] = dst & 0xff; } while(0)

#define SET_LEN_FROM_PAYLOADLEN(buf, payloadlen)	do { buf[0x00] = payloadlen + 0x09; } while(0)
#define PAYLOADLEN(buf)					(buf[0x00] - 0x09)

enum device_type {
	DEVICE_TYPE_HMCFGUSB,
	DEVICE_TYPE_CULFW,
	DEVICE_TYPE_HMUARTLGW,
};

struct hm_dev {
	int type;
	struct hmcfgusb_dev *hmcfgusb;
	struct culfw_dev *culfw;
	struct hmuartlgw_dev *hmuartlgw;
};

uint8_t* hm_sign(uint8_t *key, uint8_t *challenge, uint8_t *m_frame, uint8_t *exp_auth, uint8_t *resp);
