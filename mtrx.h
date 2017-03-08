/*
 * mtrx - Transmit and receive audio via UDP unicast or multicast
 * Copyright (C) 2014-2017 Vittorio Gambaletta <openwrt@vittgam.net>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <unistd.h>
#include <inttypes.h>
#include <errno.h>
#include <time.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netinet/ip.h>
#include <pwd.h>
#include <opus/opus.h>
#include <alsa/asoundlib.h>

struct __attribute__((__packed__)) azzp {
	int64_t tv_sec;
	uint32_t tv_nsec;
	unsigned char data;
};

struct __attribute__((__packed__)) timep {
	int64_t tv_sec;
	uint32_t tv_nsec;
};

struct __attribute__((__packed__)) timep2 {
	struct timep t1, t2;
};

struct azz {
	struct azz *next;
	uint32_t datalen;
	struct azzp packet;
};

struct mapporc {
	struct mapporc *next;
	uint32_t consumed;
	int64_t tv_sec;
	uint32_t tv_nsec;
	unsigned char pcm_data;
};

#define printverbose(...) if (verbose) fprintf(stderr, __VA_ARGS__)

#define snd_callcheck2(func, funcname, __snd_xx_retval, ...) \
	__snd_xx_retval = func(__VA_ARGS__); \
	if (__snd_xx_retval < 0) { \
		fprintf(stderr, "%s: %s\n", funcname, snd_strerror(__snd_xx_retval)); \
		exit(1); \
	}

#define snd_callcheck(func, ...) { \
	int __snd_xx_retval; \
	snd_callcheck2(func, #func, __snd_xx_retval, __VA_ARGS__); \
}

#define timeadd(time, nsecs1) { \
	int64_t __tmp_nsecs = (int64_t)(nsecs1) + (int64_t)time.tv_nsec; \
	time.tv_sec += __tmp_nsecs / 1000000000; \
	time.tv_nsec = __tmp_nsecs % 1000000000; \
	if (__tmp_nsecs < 0) { \
		time.tv_sec--; \
		time.tv_nsec += 1000000000; \
	} \
}

extern char *addr;
extern unsigned long int port;
extern char *device;
extern unsigned long int use_float;
extern unsigned long int rate;
extern unsigned long int channels;
extern unsigned long int audio_packet_duration;
extern unsigned long int buffermult;
extern unsigned long int enable_time_sync;
extern unsigned long int verbose;

extern void set_realtime_prio();
extern void drop_privs_if_needed();
extern int init_socket(int is_mrx);
extern snd_pcm_t *snd_my_init(char *device, int direction, unsigned long int rate, unsigned long int channels, unsigned long int use_float, snd_pcm_uframes_t *buffer, unsigned long int buffermult);
