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

#include "mtrx.h"

char *addr = "239.48.48.1";
unsigned long int port = 1350;
char *device = "default";
unsigned long int use_float = 0;
unsigned long int rate = 48000;
unsigned long int channels = 2;
unsigned long int audio_packet_duration = 20;
unsigned long int buffermult = 3;
unsigned long int enable_time_sync = 1;
unsigned long int verbose = 0;

void set_realtime_prio() {
	struct sched_param sp;
	if (sched_getparam(0, &sp)) {
		perror("sched_getparam");
	} else if ((sp.sched_priority = 80) > sched_get_priority_max(SCHED_FIFO)) {
		fprintf(stderr, "System does not support realtime priority\n");
	} else if (sched_setscheduler(0, SCHED_FIFO, &sp)) {
		perror("sched_setscheduler");
	}
}

void drop_privs_if_needed() {
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

int init_socket(int is_mrx) {
	int sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock < 0) {
		perror("socket");
		exit(1);
	}

	int is_mrx_multicast = 0;
	struct ip_mreq mreq;
	if (is_mrx) {
		mreq.imr_multiaddr.s_addr = inet_addr(addr);
		is_mrx_multicast = (ntohl(mreq.imr_multiaddr.s_addr) & 0xf0000000) == 0xe0000000;
		if (is_mrx_multicast) {
			unsigned int one = 1;
			if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) < 0) {
				perror("setsockopt(SO_REUSEADDR)");
				exit(1);
			}
		}
	}

	unsigned int iptos = IPTOS_DSCP_EF;
	if (setsockopt(sock, IPPROTO_IP, IP_TOS, &iptos, sizeof(iptos)) < 0) {
		perror("setsockopt(IP_TOS)");
		exit(1);
	}

	struct sockaddr_in addrin;
	memset(&addrin, 0, sizeof(addrin));
	addrin.sin_family = AF_INET;
	addrin.sin_addr.s_addr = htonl(INADDR_ANY);
	addrin.sin_port = htons(is_mrx ? (uint16_t)port : 0);
	if (bind(sock, (struct sockaddr *) &addrin, sizeof(addrin)) < 0) {
		perror("bind");
		exit(1);
	}

	if (is_mrx_multicast) {
		mreq.imr_interface.s_addr = htonl(INADDR_ANY);
		if (setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0) {
			perror("setsockopt(IP_ADD_MEMBERSHIP)");
			exit(1);
		}
	}

	return sock;
}

snd_pcm_t *snd_my_init(char *device, int direction, unsigned long int rate, unsigned long int channels, unsigned long int use_float, snd_pcm_uframes_t *buffer, unsigned long int buffermult) {
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
	snd_callcheck(snd_pcm_sw_params_set_start_threshold, snd, sw, direction == SND_PCM_STREAM_PLAYBACK ? *buffer : samples);
	snd_callcheck(snd_pcm_sw_params_set_stop_threshold, snd, sw, direction == SND_PCM_STREAM_PLAYBACK ? *buffer - samples : *buffer);
	snd_callcheck(snd_pcm_sw_params, snd, sw);
	return snd;
}
