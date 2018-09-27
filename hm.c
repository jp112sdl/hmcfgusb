/* HomeMatic protocol-functions
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
#include <sys/time.h>

#include "aes.h"
#include "hexdump.h"
#include "hm.h"

static int debug = 0;

uint8_t* hm_sign(uint8_t *key, uint8_t *challenge, uint8_t *m_frame, uint8_t *exp_auth, uint8_t *resp)
{
	uint8_t signkey[16];
	WORD ks[60];
	struct timeval tv;
	int i;

	printf("AES-request with challenge: %02x%02x%02x%02x%02x%02x\n",
			challenge[0], challenge[1], challenge[2],
			challenge[3], challenge[4], challenge[5]);

	/*
	 * Build signing key by XORing the first 6 bytes of
	 * the key with the challenge.
	 */
	memcpy(signkey, key, sizeof(signkey));
	for (i = 0; i < 6; i++) {
		signkey[i] ^= challenge[i];
	}
	aes_key_setup(signkey, ks, 128);

	/*
	 * Generate payload for first encryption.
	 */
	gettimeofday(&tv, NULL);
	resp[0] = tv.tv_sec >> 24 & 0xff;
	resp[1] = tv.tv_sec >> 16 & 0xff;
	resp[2] = tv.tv_sec >> 8 & 0xff;
	resp[3] = tv.tv_sec & 0xff;
	resp[4] = tv.tv_usec >> 8 & 0xff;
	resp[5] = tv.tv_usec & 0xff;
	memcpy(&(resp[6]), &(m_frame[MSGID]), 10);

	if (debug)
		hexdump(resp, 16, "P   > ");

	aes_encrypt(resp, resp, ks, 128);

	if (debug)
		hexdump(resp, 16, "Pe  > ");

	/*
	 * Device has to answer with the first 4 bytes of the
	 * encrypted payload to authenticate, so pass them to
	 * the caller.
	 */

	if (exp_auth) {
		memcpy(exp_auth, resp, 4);
	}

	/*
	 * XOR parameters of the m_frame to the payload.
	 */
	for (i = 0; i < PAYLOADLEN(m_frame) - 1; i++) {
		if (i == 16) {
			break;
		}

		resp[i] ^= m_frame[PAYLOAD + 1 + i];
	}

	if (debug)
		hexdump(resp, 16, "Pe^ > ");

	/*
	 * Encrypt payload again
	 */
	aes_encrypt(resp, resp, ks, 128);

	if (debug)
		hexdump(resp, 16, "Pe^e> ");

	return resp;
}
