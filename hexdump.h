/* simple hexdumper
 *
 * Copyright (c) 2004-2016 Michael Gernoth <michael@gernoth.net>
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

static void asciishow(unsigned char *buf, int len)
{
	int i;

	fprintf(stderr, "  ");
	for (i = 0; i < len; i++) {
		if ((buf[i] >=32) && (buf[i] <=126)) {
			fprintf(stderr, "%c", buf[i]);
		} else {
			fprintf(stderr, ".");
		}
	}
}

static void hexdump(unsigned char *buf, int len, char *prefix)
{
	int i, j;

	fprintf(stderr, "\n%s", prefix);
	for (i = 0; i < len; i++) {
		if((i%16) == 0) {
			fprintf(stderr, "0x%04x: ", i);
		}
		fprintf(stderr, "%02x ", buf[i]);
		if ((i%16) == 15) {
			asciishow(buf+i-15, 16);
			if (i != (len-1))
				fprintf(stderr, "\n%s", prefix);
		}
	}
	for (j = (i%16); j < 16; j++)
		fprintf(stderr, "   ");
	asciishow(buf+i-(i%16), (i%16));
	fprintf(stderr, "\n");
}
