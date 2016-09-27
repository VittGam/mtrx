/*
 * mtrx - Transmit and receive audio via UDP unicast or multicast
 * Copyright (C) 2014-2016 Vittorio Gambaletta <openwrt@vittgam.net>
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
	int64_t __tmp_nsecs = (nsecs1); \
	if (__tmp_nsecs > 0) { \
		time.tv_nsec += __tmp_nsecs; \
		while (time.tv_nsec >= 1000000000) { \
			time.tv_sec++; \
			time.tv_nsec -= 1000000000; \
		} \
	} else if (__tmp_nsecs < 0) { \
		__tmp_nsecs = -__tmp_nsecs; \
		while (__tmp_nsecs >= 1000000000) { \
			time.tv_sec--; \
			__tmp_nsecs -= 1000000000; \
		} \
		if (time.tv_nsec < __tmp_nsecs) { \
			time.tv_sec--; \
			time.tv_nsec += 1000000000 - __tmp_nsecs; \
		} else { \
			time.tv_nsec -= __tmp_nsecs; \
		} \
	} \
}

static void set_realtime_prio() {
	struct sched_param sp;
	if (sched_getparam(0, &sp)) {
		perror("sched_getparam");
	} else if ((sp.sched_priority = 80) > sched_get_priority_max(SCHED_FIFO)) {
		fprintf(stderr, "System does not support realtime priority\n");
	} else if (sched_setscheduler(0, SCHED_FIFO, &sp)) {
		perror("sched_setscheduler");
	}
}

static void drop_privs_if_needed() {
	if (getuid() != 0 && geteuid() != 0) {
		return;
	}
	errno = 0;
	struct passwd *pw = getpwnam("nobody");
	if (pw && pw->pw_uid > 0 && pw->pw_gid > 0) {
		errno = 0;
		if (setgid(pw->pw_gid)) {
			perror("setgid");
			exit(1);
		}
		errno = 0;
		if (setuid(pw->pw_uid)) {
			perror("setuid");
			exit(1);
		}
	} else {
		perror("getpwnam: user nobody not found");
		exit(1);
	}
	fprintf(stderr, "Successfully dropped root privileges\n");
}

static snd_pcm_t *snd_my_init(char *device, int direction, unsigned long int rate, unsigned long int channels, unsigned long int use_float, snd_pcm_uframes_t *buffer, unsigned long int buffermult) {
	snd_pcm_t *snd = NULL;
	int dir = 0;
	snd_pcm_hw_params_t *hw;
	snd_pcm_sw_params_t *sw;
	snd_pcm_hw_params_alloca(&hw);
	snd_pcm_sw_params_alloca(&sw);
	snd_callcheck(snd_pcm_open, &snd, device, direction, direction == SND_PCM_STREAM_PLAYBACK ? SND_PCM_NONBLOCK : 0);
	snd_callcheck(snd_pcm_hw_params_any, snd, hw);
	snd_callcheck(snd_pcm_hw_params_set_rate_resample, snd, hw, 0);
	snd_callcheck(snd_pcm_hw_params_set_access, snd, hw, SND_PCM_ACCESS_RW_INTERLEAVED);
	snd_callcheck(snd_pcm_hw_params_set_format, snd, hw, use_float ? SND_PCM_FORMAT_FLOAT : SND_PCM_FORMAT_S16);
	snd_callcheck(snd_pcm_hw_params_set_rate, snd, hw, rate, 0);
	snd_callcheck(snd_pcm_hw_params_set_channels, snd, hw, channels);
	snd_pcm_uframes_t samples = *buffer;
	snd_callcheck(snd_pcm_hw_params_set_period_size_near, snd, hw, buffer, &dir);
	*buffer *= buffermult;
	snd_callcheck(snd_pcm_hw_params_set_buffer_size_near, snd, hw, buffer);
	snd_callcheck(snd_pcm_hw_params, snd, hw);
	snd_callcheck(snd_pcm_sw_params_current, snd, sw);
	snd_callcheck(snd_pcm_sw_params_set_start_threshold, snd, sw, *buffer);
	snd_callcheck(snd_pcm_sw_params_set_stop_threshold, snd, sw, direction == SND_PCM_STREAM_PLAYBACK ? *buffer - samples : *buffer);
	snd_callcheck(snd_pcm_sw_params, snd, sw);
	return snd;
}
