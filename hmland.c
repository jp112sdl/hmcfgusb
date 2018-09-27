/* HM-CFG-LAN emulation for HM-CFG-USB
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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <strings.h>
#include <poll.h>
#include <signal.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <libusb-1.0/libusb.h>

#include "version.h"
#include "hexdump.h"
#include "hmcfgusb.h"
#include "util.h"

#define PID_FILE "/var/run/hmland.pid"

#define POLL_TIMEOUT_MS		250	/* Wake up device/bus at least once every 250ms */
#define DEFAULT_REBOOT_SECONDS	86400
#define LAN_READ_CHUNK_SIZE	2048
/* Don't allow remote clients to consume all of our memory */
#define LAN_MAX_LINE_LENGTH	4096
#define LAN_MAX_BUF_LENGTH	1048576

extern char *optarg;

static int impersonate_hmlanif = 0;
static int debug = 0;
static int verbose = 0;
static FILE *logfile = NULL;
static int reboot_seconds = 0;
static int reboot_at_hour = -1;
static int reboot_at_minute = -1;
static int reboot_set = 0;
static uint8_t *lan_read_buf = NULL;
static int lan_read_buflen = 0;
static char *serial = NULL;

struct queued_rx {
	char *rx;
	int len;
	struct queued_rx *next;
};

static struct queued_rx *qrx = NULL;
static int wait_for_h = 0;

#define	FLAG_LENGTH_BYTE	(1<<0)
#define	FLAG_FORMAT_HEX		(1<<1)
#define	FLAG_COMMA_BEFORE	(1<<2)
#define	FLAG_COMMA_AFTER	(1<<3)
#define	FLAG_NL			(1<<4)
#define	FLAG_IGNORE_COMMAS	(1<<5)

#define CHECK_SPACE(x)		if ((*outpos + x) > outend) { fprintf(stderr, "Not enough space!\n"); return 0; }
#define CHECK_AVAIL(x)		if ((*inpos + x) > inend) { fprintf(stderr, "Not enough input available!\n"); return 0; }

static void print_timestamp(FILE *f)
{
	struct timeval tv;
	struct tm *tmp;
	char ts[32];

	gettimeofday(&tv, NULL);
	tmp = localtime(&tv.tv_sec);
	memset(ts, 0, sizeof(ts));
	strftime(ts, sizeof(ts)-1, "%Y-%m-%d %H:%M:%S", tmp);
	fprintf(f, "%s.%06ld: ", ts, tv.tv_usec);
}

static void write_log(char *buf, int len, char *fmt, ...)
{
	va_list ap;
	int i;

	if ((!logfile) && (!verbose))
		return;

	if (logfile)
		print_timestamp(logfile);
	if (verbose)
		print_timestamp(stdout);

	if (fmt) {
		if (logfile) {
			va_start(ap, fmt);
			vfprintf(logfile, fmt, ap);
			va_end(ap);
		}
		if (verbose) {
			va_start(ap, fmt);
			vprintf(fmt, ap);
			va_end(ap);
		}
	}

	if (buf && len) {
		for (i = 0; i < len; i++) {
			if (logfile)
				fprintf(logfile, "%c", buf[i]);
			if (verbose)
				printf("%c", buf[i]);
		}
		if (logfile)
			fprintf(logfile, "\n");
		if (verbose)
			printf("\n");
	}
	if (logfile)
		fflush(logfile);
}

static int format_part_out(uint8_t **inpos, int inlen, uint8_t **outpos, int outlen, int len, int flags)
{
	uint8_t *buf_out = *outpos;
	uint8_t *outend = *outpos + outlen;
	uint8_t *inend = *inpos + inlen;
	int i;

	if (flags & FLAG_COMMA_BEFORE) {
		CHECK_SPACE(1);
		**outpos=',';
		*outpos += 1;
	}

	if (flags & FLAG_LENGTH_BYTE) {
		CHECK_AVAIL(1);
		len = **inpos;
		*inpos += 1;
	}

	if (flags & FLAG_FORMAT_HEX) {
		CHECK_AVAIL(len);
		CHECK_SPACE(len*2);
		for (i = 0; i < len; i++) {
			**outpos = nibble_to_ascii(((**inpos) & 0xf0) >> 4);
			*outpos += 1;
			**outpos = nibble_to_ascii(((**inpos) & 0xf));
			*inpos += 1; *outpos += 1;
		}
	} else {
		CHECK_AVAIL(len);
		CHECK_SPACE(len);
		memcpy(*outpos, *inpos, len);
		*outpos += len;
		*inpos += len;
	}

	if (flags & FLAG_COMMA_AFTER) {
		CHECK_SPACE(1);
		**outpos=',';
		*outpos += 1;
	}

	if (flags & FLAG_NL) {
		CHECK_SPACE(2);
		**outpos='\r';
		*outpos += 1;
		**outpos='\n';
		*outpos += 1;
	}

	return *outpos - buf_out;
}

static int parse_part_in(uint8_t **inpos, int inlen, uint8_t **outpos, int outlen, int flags)
{
	uint8_t *buf_out = *outpos;
	uint8_t *outend = *outpos + outlen;
	uint8_t *inend = *inpos + inlen;

	if (flags & FLAG_LENGTH_BYTE) {
		int len = 0;
		uint8_t *ip;

		ip = *inpos;
		while(ip < inend) {
			if (*ip == ',') {
				ip++;
				if (!(flags & FLAG_IGNORE_COMMAS))
					break;

				continue;
			}
			len++;
			ip++;
		}
		CHECK_SPACE(1);
		**outpos = (len / 2);
		*outpos += 1;
	}

	while(*inpos < inend) {
		if (**inpos == ',') {
			*inpos += 1;
			if (!(flags & FLAG_IGNORE_COMMAS))
				break;

			continue;
		}

		CHECK_SPACE(1);
		CHECK_AVAIL(2);

		**outpos = ascii_to_nibble(**inpos) << 4;
		*inpos += 1;
		**outpos |= ascii_to_nibble(**inpos);
		*inpos += 1; *outpos += 1;
	}

	return *outpos - buf_out;
}

static int hmlan_format_out(uint8_t *buf, int buf_len, void *data)
{
	uint8_t out[1024];
	uint8_t *outpos;
	uint8_t *inpos;
	uint16_t version;
	int fd = *((int*)data);
	int w;

	if (buf_len < 1)
		return 1;

	memset(out, 0, sizeof(out));
	outpos = out;
	inpos = buf;

	format_part_out(&inpos, (buf_len-(inpos-buf)), &outpos, (sizeof(out)-(outpos-out)), 1, 0);
	switch(buf[0]) {
		case 'H':
			if (impersonate_hmlanif) {
				buf[5] = 'L';
				buf[6] = 'A';
				buf[7] = 'N';
			}
			format_part_out(&inpos, (buf_len-(inpos-buf)), &outpos, (sizeof(out)-(outpos-out)), 0, FLAG_LENGTH_BYTE);
			version = inpos[0] << 8;
			version |= inpos[1];
			format_part_out(&inpos, (buf_len-(inpos-buf)), &outpos, (sizeof(out)-(outpos-out)), 2, FLAG_FORMAT_HEX | FLAG_COMMA_BEFORE);
			format_part_out(&inpos, (buf_len-(inpos-buf)), &outpos, (sizeof(out)-(outpos-out)), 0, FLAG_COMMA_BEFORE | FLAG_LENGTH_BYTE);
			format_part_out(&inpos, (buf_len-(inpos-buf)), &outpos, (sizeof(out)-(outpos-out)), 3, FLAG_FORMAT_HEX | FLAG_COMMA_BEFORE);
			format_part_out(&inpos, (buf_len-(inpos-buf)), &outpos, (sizeof(out)-(outpos-out)), 3, FLAG_FORMAT_HEX | FLAG_COMMA_BEFORE);
			format_part_out(&inpos, (buf_len-(inpos-buf)), &outpos, (sizeof(out)-(outpos-out)), 4, FLAG_FORMAT_HEX | FLAG_COMMA_BEFORE);
			if (version < 0x03c7) {
				format_part_out(&inpos, (buf_len-(inpos-buf)), &outpos, (sizeof(out)-(outpos-out)), 2, FLAG_FORMAT_HEX | FLAG_COMMA_BEFORE | FLAG_NL);
			} else {
				format_part_out(&inpos, (buf_len-(inpos-buf)), &outpos, (sizeof(out)-(outpos-out)), 2, FLAG_FORMAT_HEX | FLAG_COMMA_BEFORE);
				format_part_out(&inpos, (buf_len-(inpos-buf)), &outpos, (sizeof(out)-(outpos-out)), 1, FLAG_FORMAT_HEX | FLAG_COMMA_BEFORE | FLAG_NL);
			}

			if (!reboot_set) {
				int new_reboot_seconds;

				if (version < 0x03c7) {
					new_reboot_seconds = DEFAULT_REBOOT_SECONDS;
				} else {
					new_reboot_seconds = 0;
				}

				if (verbose && new_reboot_seconds && (reboot_seconds != new_reboot_seconds))
					printf("Rebooting in %u seconds due to old firmware (0.%d)\n",
						new_reboot_seconds, version);

				reboot_seconds = new_reboot_seconds;
			}

			break;
		case 'E':
			format_part_out(&inpos, (buf_len-(inpos-buf)), &outpos, (sizeof(out)-(outpos-out)), 3, FLAG_FORMAT_HEX);
			format_part_out(&inpos, (buf_len-(inpos-buf)), &outpos, (sizeof(out)-(outpos-out)), 2, FLAG_FORMAT_HEX | FLAG_COMMA_BEFORE);
			format_part_out(&inpos, (buf_len-(inpos-buf)), &outpos, (sizeof(out)-(outpos-out)), 4, FLAG_FORMAT_HEX | FLAG_COMMA_BEFORE);
			format_part_out(&inpos, (buf_len-(inpos-buf)), &outpos, (sizeof(out)-(outpos-out)), 1, FLAG_FORMAT_HEX | FLAG_COMMA_BEFORE);
			format_part_out(&inpos, (buf_len-(inpos-buf)), &outpos, (sizeof(out)-(outpos-out)), 2, FLAG_FORMAT_HEX | FLAG_COMMA_BEFORE);
			format_part_out(&inpos, (buf_len-(inpos-buf)), &outpos, (sizeof(out)-(outpos-out)), 0, FLAG_FORMAT_HEX | FLAG_COMMA_BEFORE | FLAG_LENGTH_BYTE | FLAG_NL);

			break;
		case 'R':
			format_part_out(&inpos, (buf_len-(inpos-buf)), &outpos, (sizeof(out)-(outpos-out)), 4, FLAG_FORMAT_HEX);
			format_part_out(&inpos, (buf_len-(inpos-buf)), &outpos, (sizeof(out)-(outpos-out)), 2, FLAG_FORMAT_HEX | FLAG_COMMA_BEFORE);
			format_part_out(&inpos, (buf_len-(inpos-buf)), &outpos, (sizeof(out)-(outpos-out)), 4, FLAG_FORMAT_HEX | FLAG_COMMA_BEFORE);
			format_part_out(&inpos, (buf_len-(inpos-buf)), &outpos, (sizeof(out)-(outpos-out)), 1, FLAG_FORMAT_HEX | FLAG_COMMA_BEFORE);
			format_part_out(&inpos, (buf_len-(inpos-buf)), &outpos, (sizeof(out)-(outpos-out)), 2, FLAG_FORMAT_HEX | FLAG_COMMA_BEFORE);
			format_part_out(&inpos, (buf_len-(inpos-buf)), &outpos, (sizeof(out)-(outpos-out)), 0, FLAG_FORMAT_HEX | FLAG_COMMA_BEFORE | FLAG_LENGTH_BYTE | FLAG_NL);

			break;
		case 'I':
			format_part_out(&inpos, (buf_len-(inpos-buf)), &outpos, (sizeof(out)-(outpos-out)), 1, FLAG_FORMAT_HEX);
			format_part_out(&inpos, (buf_len-(inpos-buf)), &outpos, (sizeof(out)-(outpos-out)), 1, FLAG_FORMAT_HEX | FLAG_COMMA_BEFORE);
			format_part_out(&inpos, (buf_len-(inpos-buf)), &outpos, (sizeof(out)-(outpos-out)), 1, FLAG_FORMAT_HEX | FLAG_COMMA_BEFORE);
			format_part_out(&inpos, (buf_len-(inpos-buf)), &outpos, (sizeof(out)-(outpos-out)), 1, FLAG_FORMAT_HEX | FLAG_COMMA_BEFORE | FLAG_NL);

			break;
		case 'G':
			format_part_out(&inpos, (buf_len-(inpos-buf)), &outpos, (sizeof(out)-(outpos-out)), 1, FLAG_FORMAT_HEX | FLAG_NL);

			break;
		default:
			format_part_out(&inpos, (buf_len-(inpos-buf)), &outpos, (sizeof(out)-(outpos-out)), buf_len-1, FLAG_FORMAT_HEX | FLAG_NL);
			hexdump(buf, buf_len, "Unknown> ");
			break;
	}

	/* Queue packet until first respone to 'K' is received */
	if (wait_for_h && buf[0] != 'H') {
		struct queued_rx **rxp = &qrx;

		while (*rxp)
			rxp = &((*rxp)->next);

		*rxp = malloc(sizeof(struct queued_rx));
		if (!*rxp) {
			perror("malloc");
			return 0;
		}

		memset(*rxp, 0, sizeof(struct queued_rx));
		(*rxp)->len = outpos-out;
		(*rxp)->rx = malloc((*rxp)->len);
		if (!(*rxp)->rx) {
			perror("malloc");
			return 0;
		}
		memset((*rxp)->rx, 0, (*rxp)->len);
		memcpy((*rxp)->rx, out, (*rxp)->len);

		return 1;
	}

	write_log((char*)out, outpos-out-2, "LAN < ");

	w = write(fd, out, outpos-out);
	if (w <= 0) {
		perror("write");
		return 0;
	}

	/* Send all queued packets */
	if (wait_for_h) {
		struct queued_rx *curr_rx = qrx;
		struct queued_rx *last_rx;

		while (curr_rx) {
			write_log(curr_rx->rx, curr_rx->len-2, "LAN < ");

			w = write(fd, curr_rx->rx, curr_rx->len);
			if (w <= 0) {
				perror("write");
			}
			last_rx = curr_rx;
			curr_rx = curr_rx->next;

			free(last_rx->rx);
			free(last_rx);
		}

		qrx = NULL;

		wait_for_h = 0;
	}

	return 1;
}

static int hmlan_parse_one(uint8_t *cmd, int last, void *data)
{
	struct hmcfgusb_dev *dev = data;
	uint8_t out[0x40]; //FIXME!!!
	uint8_t *outpos;
	uint8_t *inpos = cmd;

	outpos = out;

	if (last == 0)
		return 1;

	write_log((char*)cmd, last,  "LAN > ");

	memset(out, 0, sizeof(out));
	*outpos++ = *inpos++;

	switch(*cmd) {
		case 'S':
			parse_part_in(&inpos, (last-(inpos-cmd)), &outpos, (sizeof(out)-(outpos-out)), 0);
			parse_part_in(&inpos, (last-(inpos-cmd)), &outpos, (sizeof(out)-(outpos-out)), 0);
			parse_part_in(&inpos, (last-(inpos-cmd)), &outpos, (sizeof(out)-(outpos-out)), 0);
			parse_part_in(&inpos, (last-(inpos-cmd)), &outpos, (sizeof(out)-(outpos-out)), 0);
			parse_part_in(&inpos, (last-(inpos-cmd)), &outpos, (sizeof(out)-(outpos-out)), 0);
			parse_part_in(&inpos, (last-(inpos-cmd)), &outpos, (sizeof(out)-(outpos-out)), FLAG_LENGTH_BYTE);
			break;
		case 'Y':
			parse_part_in(&inpos, (last-(inpos-cmd)), &outpos, (sizeof(out)-(outpos-out)), 0);
			parse_part_in(&inpos, (last-(inpos-cmd)), &outpos, (sizeof(out)-(outpos-out)), 0);
			parse_part_in(&inpos, (last-(inpos-cmd)), &outpos, (sizeof(out)-(outpos-out)), FLAG_LENGTH_BYTE);
			break;
		case '+':
			parse_part_in(&inpos, (last-(inpos-cmd)), &outpos, (sizeof(out)-(outpos-out)), 0);
			parse_part_in(&inpos, (last-(inpos-cmd)), &outpos, (sizeof(out)-(outpos-out)), 0);
			parse_part_in(&inpos, (last-(inpos-cmd)), &outpos, (sizeof(out)-(outpos-out)), 0);
			parse_part_in(&inpos, (last-(inpos-cmd)), &outpos, (sizeof(out)-(outpos-out)), FLAG_LENGTH_BYTE);
		default:
			parse_part_in(&inpos, (last-(inpos-cmd)), &outpos, (sizeof(out)-(outpos-out)), FLAG_IGNORE_COMMAS);
			break;
	}

	hmcfgusb_send(dev, out, sizeof(out), 1);

	return 1;
}

static int hmlan_parse_in(int fd, void *data)
{
	uint8_t *newbuf;
	int r;
	int i;

	newbuf = realloc(lan_read_buf, lan_read_buflen + LAN_READ_CHUNK_SIZE);
	if (!newbuf) {
		perror("realloc");
		return 0;
	}
	lan_read_buf = newbuf;
	r = read(fd, lan_read_buf + lan_read_buflen, LAN_READ_CHUNK_SIZE);
	if (r > 0) {
		lan_read_buflen += r;
		if (lan_read_buflen > LAN_MAX_BUF_LENGTH) {
			if (verbose)
				printf("Our buffer is bigger than %d bytes (%d bytes), closing connection!\n", LAN_MAX_BUF_LENGTH, lan_read_buflen);
			return -1;
		}
		while(lan_read_buflen > 0) {
			int found = 0;

			for (i = 0; i < lan_read_buflen; i++) {
				if ((lan_read_buf[i] == '\r') || (lan_read_buf[i] == '\n')) {
					if (i > 0)
						hmlan_parse_one(lan_read_buf, i, data);
					memmove(lan_read_buf, lan_read_buf + i + 1, lan_read_buflen - (i + 1));
					lan_read_buflen -= (i + 1);
					found = 1;
					break;
				}
				if (i > LAN_MAX_LINE_LENGTH) {
					if (verbose)
						printf("Client sent more than %d bytes without newline, closing connection!\n", LAN_MAX_LINE_LENGTH);
					return -1;
				}
			}
			if (!found)
				break;
			newbuf = realloc(lan_read_buf, lan_read_buflen);
			if (lan_read_buflen && !newbuf) {
				perror("realloc");
				return 0;
			}
			lan_read_buf = newbuf;
		}
	} else if (r < 0) {
		if (errno != ECONNRESET)
			perror("read");
		return r;
	} else {
		return 0;
	}

	return 1;
}

static int comm(int fd_in, int fd_out, int master_socket, int flags)
{
	struct hmcfgusb_dev *dev;
	uint8_t out[0x40]; //FIXME!!!
	int quit = 0;

	hmcfgusb_set_debug(debug);

	dev = hmcfgusb_init(hmlan_format_out, &fd_out, serial);
	if (!dev) {
		fprintf(stderr, "Can't initialize HM-CFG-USB!\n");
		return 0;
	}

	if (dev->bootloader) {
		if (verbose)
			printf("HM-CFG-USB in bootloader mode, restarting in normal mode...\n");

		hmcfgusb_leave_bootloader(dev);

		hmcfgusb_close(dev);
		sleep(1);
		return 0;
	}

	if ((reboot_at_hour != -1) && (reboot_at_minute != -1)) {
		struct tm *tm_s;
		time_t tm;

		tm = time(NULL);
		tm_s = localtime(&tm);
		if (tm_s == NULL) {
			perror("localtime");
			return 0;
		}

		tm_s->tm_hour = reboot_at_hour;
		tm_s->tm_min = reboot_at_minute;
		tm_s->tm_sec = 0;

		tm = mktime(tm_s);
		reboot_seconds = tm - dev->opened_at;

		while (reboot_seconds <= 0)
			reboot_seconds += 86400;
	}

	if (verbose && reboot_seconds)
		printf("Rebooting in %u seconds\n", reboot_seconds);


	if (!hmcfgusb_add_pfd(dev, fd_in, POLLIN)) {
		fprintf(stderr, "Can't add client to pollfd!\n");
		hmcfgusb_close(dev);
		return 0;
	}

	if (master_socket >= 0) {
		if (!hmcfgusb_add_pfd(dev, master_socket, POLLIN)) {
			fprintf(stderr, "Can't add master_socket to pollfd!\n");
			hmcfgusb_close(dev);
			return 0;
		}
	}

	memset(out, 0, sizeof(out));
	out[0] = 'K';
	wait_for_h = 1;
	hmcfgusb_send_null_frame(dev, 1);
	hmcfgusb_send(dev, out, sizeof(out), 1);

	while(!quit) {
		int fd;

		fd = hmcfgusb_poll(dev, POLL_TIMEOUT_MS);
		if (fd >= 0) {
			if (fd == master_socket) {
				int client;

				client = accept(master_socket, NULL, 0);
				if (client >= 0) {
					shutdown(client, SHUT_RDWR);
					close(client);
				}
			} else {
				if (hmlan_parse_in(fd, dev) <= 0) {
					quit = 1;
				}
			}
		} else if (fd == -1) {
			if (errno) {
				if (errno != ETIMEDOUT) {
					perror("hmcfgusb_poll");
					quit = 1;
				} else {
					/* periodically wakeup the device */
					hmcfgusb_send_null_frame(dev, 1);
					if (wait_for_h) {
						memset(out, 0, sizeof(out));
						out[0] = 'K';
						hmcfgusb_send(dev, out, sizeof(out), 1);
					}
				}
			}
		}

		if (reboot_seconds && ((dev->opened_at + reboot_seconds) <= time(NULL))) {
			if (verbose) {
				printf("HM-CFG-USB running since %lu seconds, rebooting now...\n",
					time(NULL) - dev->opened_at);
			}
			hmcfgusb_enter_bootloader(dev);
		}
	}

	hmcfgusb_close(dev);
	return 1;
}

void sigterm_handler(int sig)
{
	if (unlink(PID_FILE) == -1)
		perror("Can't remove PID file");

	exit(EXIT_SUCCESS);
}

#define FLAG_DAEMON	(1 << 0)
#define FLAG_PID_FILE	(1 << 1)

static int socket_server(char *iface, int port, int flags)
{
	struct sigaction sact;
	struct sockaddr_in sin;
	int sock;
	int n;
	pid_t pid;

	if (flags & FLAG_DAEMON) {
		FILE *pidfile = NULL;

		if (flags & FLAG_PID_FILE) {
			int fd;

			fd = open(PID_FILE, O_CREAT | O_EXCL | O_WRONLY, 0644);
			if (fd == -1) {
				if (errno == EEXIST) {
					pid_t old_pid;
					pidfile = fopen(PID_FILE, "r");
					if (!pidfile) {
						perror("PID file " PID_FILE " already exists, already running?");
						exit(EXIT_FAILURE);
					}

					if (fscanf(pidfile, "%u", &old_pid) != 1) {
						fclose(pidfile);
						fprintf(stderr, "Can't read old PID from " PID_FILE ", already running?\n");
						exit(EXIT_FAILURE);
					}

					fclose(pidfile);

					fprintf(stderr, "Already running with PID %u according to " PID_FILE "!\n", old_pid);
					exit(EXIT_FAILURE);
				}
				perror("Can't create PID file " PID_FILE);
				exit(EXIT_FAILURE);
			}

			pidfile = fdopen(fd, "w");
			if (!pidfile) {
				perror("Can't reopen PID file fd");
				exit(EXIT_FAILURE);
			}

			memset(&sact, 0, sizeof(sact));
			sact.sa_handler = sigterm_handler;

			if (sigaction(SIGTERM, &sact, NULL) == -1) {
				perror("sigaction(SIGTERM)");
				exit(EXIT_FAILURE);
			}
		}

		pid = fork();
		if (pid > 0) {
			if (pidfile) {
				fprintf(pidfile, "%u\n", pid);
				fclose(pidfile);
			}

			printf("Daemon with PID %u started!\n", pid);
			exit(EXIT_SUCCESS);
		} else if (pid < 0) {
			perror("fork");
			exit(EXIT_FAILURE);
		}

		if (pidfile)
			fclose(pidfile);
	}

	memset(&sact, 0, sizeof(sact));
	sact.sa_handler = SIG_IGN;

	if (sigaction(SIGPIPE, &sact, NULL) == -1) {
		perror("sigaction(SIGPIPE)");
		exit(EXIT_FAILURE);
	}

	sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (sock == -1) {
		perror("Can't open socket");
		return EXIT_FAILURE;
	}

	n = 1;
	if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &n, sizeof(n)) == -1) {
		perror("Can't set socket options");
		return EXIT_FAILURE;
	}

	memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_port = htons(port);
	if (!iface) {
		sin.sin_addr.s_addr = htonl(INADDR_ANY);
	} else {
		if (inet_pton(AF_INET, iface, &(sin.sin_addr.s_addr)) != 1) {
			fprintf(stderr, "Can't convert IP %s, aborting!\n", iface);
			return EXIT_FAILURE;
		}
	}

	if (bind(sock, (struct sockaddr*)&sin, sizeof(sin)) == -1) {
		perror("Can't bind socket");
		return EXIT_FAILURE;
	}

	if (listen(sock, 1) == -1) {
		perror("Can't listen on socket");
		return EXIT_FAILURE;
	}

	while(1) {
		struct sockaddr_in csin;
		socklen_t csinlen;
		int client;
		in_addr_t client_addr;

		memset(&csin, 0, sizeof(csin));
		csinlen = sizeof(csin);
		client = accept(sock, (struct sockaddr*)&csin, &csinlen);
		if (client == -1) {
			perror("Couldn't accept client");
			continue;
		}

		/* FIXME: getnameinfo... */
		client_addr = ntohl(csin.sin_addr.s_addr);

		write_log(NULL, 0, "Client %d.%d.%d.%d connected!\n",
				(client_addr & 0xff000000) >> 24,
				(client_addr & 0x00ff0000) >> 16,
				(client_addr & 0x0000ff00) >> 8,
				(client_addr & 0x000000ff));

		comm(client, client, sock, flags);

		shutdown(client, SHUT_RDWR);
		close(client);

		if (lan_read_buf)
			free(lan_read_buf);
		lan_read_buf = NULL;
		lan_read_buflen = 0;

		write_log(NULL, 0, "Connection to %d.%d.%d.%d closed!\n",
				(client_addr & 0xff000000) >> 24,
				(client_addr & 0x00ff0000) >> 16,
				(client_addr & 0x0000ff00) >> 8,
				(client_addr & 0x000000ff));
		sleep(1);
	}

	return EXIT_SUCCESS;
}

static int interactive_server(int flags)
{
	if (!comm(STDIN_FILENO, STDOUT_FILENO, -1, flags))
		return EXIT_FAILURE;

	return EXIT_SUCCESS;
}

void hmlan_syntax(char *prog)
{
	fprintf(stderr, "Syntax: %s options\n\n", prog);
	fprintf(stderr, "Possible options:\n");
	fprintf(stderr, "\t-D\t\tdebug mode\n");
	fprintf(stderr, "\t-d\t\tdaemon mode\n");
	fprintf(stderr, "\t-h\t\tthis help\n");
	fprintf(stderr, "\t-I\t\tpretend to be HM-LAN-IF for compatibility with client-software (previous default)\n");
	fprintf(stderr, "\t-i\t\tinteractive mode (connect HM-CFG-USB to terminal)\n");
	fprintf(stderr, "\t-l ip\t\tlisten on given IP address only (for example 127.0.0.1)\n");
	fprintf(stderr, "\t-L logfile\tlog network-communication to logfile\n");
	fprintf(stderr, "\t-P\t\tcreate PID file " PID_FILE " in daemon mode\n");
	fprintf(stderr, "\t-p n\t\tlisten on port n (default: 1000)\n");
	fprintf(stderr, "\t-r n\t\treboot HM-CFG-USB after n seconds (0: no reboot, default: %u if FW < 0.967, 0 otherwise)\n", DEFAULT_REBOOT_SECONDS);
	fprintf(stderr, "\t   hh:mm\treboot HM-CFG-USB daily at hh:mm\n");
	fprintf(stderr, "\t-S serial\tuse HM-CFG-USB with given serial (for multiple hmland instances)\n");
	fprintf(stderr, "\t-v\t\tverbose mode\n");
	fprintf(stderr, "\t-V\t\tshow version (" VERSION ")\n");

}

int main(int argc, char **argv)
{
	int port = 1000;
	char *iface = NULL;
	int interactive = 0;
	int flags = 0;
	char *ep;
	int opt;
	
	while((opt = getopt(argc, argv, "DdhIiPp:Rr:l:L:S:vV")) != -1) {
		switch (opt) {
			case 'D':
				debug = 1;
				verbose = 1;
				break;
			case 'd':
				flags |= FLAG_DAEMON;
				break;
			case 'I':
				impersonate_hmlanif = 1;
				break;
			case 'i':
				interactive = 1;
				break;
			case 'P':
				flags |= FLAG_PID_FILE;
				break;
			case 'p':
				port = strtoul(optarg, &ep, 10);
				if (*ep != '\0') {
					fprintf(stderr, "Can't parse port!\n");
					exit(EXIT_FAILURE);
				}
				break;
			case 'R':
				fprintf(stderr, "-R is no longer needed (1s wakeup is default)\n");
				break;
			case 'r':
				reboot_seconds = strtoul(optarg, &ep, 10);
				if (*ep != '\0') {
					if (*ep == ':') {
						reboot_at_hour = reboot_seconds;
						ep++;
						reboot_at_minute = strtoul(ep, &ep, 10);
						if (*ep != '\0') {
							fprintf(stderr, "Can't parse reboot-time!\n");
							exit(EXIT_FAILURE);
						}

						reboot_seconds = 0;
					} else {
						fprintf(stderr, "Can't parse reboot-timeout!\n");
						exit(EXIT_FAILURE);
					}
				}
				reboot_set = 1;
				break;
			case 'l':
				iface = optarg;
				break;
			case 'L':
				logfile = fopen(optarg, "a");
				if (!logfile) {
					perror("fopen(logfile)");
					exit(EXIT_FAILURE);
				}
				break;
			case 'S':
				serial = optarg;
				break;
			case 'v':
				verbose = 1;
				break;
			case 'V':
				printf("hmland " VERSION "\n");
				printf("Copyright (c) 2013-16 Michael Gernoth\n\n");
				exit(EXIT_SUCCESS);
			case 'h':
			case ':':
			case '?':
			default:
				hmlan_syntax(argv[0]);
				exit(EXIT_FAILURE);
				break;
		}
	}
	
	if (interactive) {
		return interactive_server(flags);
	} else {
		return socket_server(iface, port, flags);
	}
}
