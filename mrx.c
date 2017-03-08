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

static signed long int delay = 80;
static struct azz *audio_buffer = NULL;
static pthread_barrier_t init_barrier;
static pthread_mutex_t audio_mutex, time_mutex;
static struct timespec last_packet_clock = {0, 0};
static int64_t server_time_diff_global = 0;

static void *audio_playback_thread(void *arg) {
	printverbose("Audio playback thread started\n");

	snd_pcm_uframes_t samples = audio_packet_duration * rate / 1000;
	size_t pcm_size_multiplier = (use_float ? sizeof(float) : sizeof(int16_t)) * channels;
	size_t pcm_size = samples * pcm_size_multiplier;
	uint64_t clock_period = (uint64_t) 1000000 * audio_packet_duration;

	int error;
	OpusDecoder *decoder = opus_decoder_create(rate, channels, &error);
	if (decoder == NULL) {
		fprintf(stderr, "opus_decoder_create: %s\n", opus_strerror(error));
		exit(1);
	}

	void *pcm = alloca(pcm_size);
	struct timespec clock = {0, 0};

	snd_pcm_t *snd = NULL;
	snd_pcm_uframes_t buffer = samples;
	int64_t delay2 = (int64_t) delay * -1000000;
	if (strcmp(device, "-") != 0) {
		snd = snd_my_init(device, SND_PCM_STREAM_PLAYBACK, rate, channels, use_float, &buffer, buffermult);
		delay2 += (int64_t) buffer * 1000000000 / rate;
		delay -= (int64_t) buffer * 1000 / rate;
	}
	int64_t delay1 = (int64_t) ((delay2 < 0 ? -delay2 : delay2) % clock_period) * (delay2 < 0 ? 1 : -1);

	if (delay < 0) {
		fprintf(stderr, "Total audio delay minus ALSA delay (%ld) cannot be negative.\n", delay);
		exit(1);
	}

	pthread_barrier_wait(&init_barrier);

	while (1) {
		struct azz *currframe = NULL;

		int64_t server_time_diff;
		pthread_mutex_lock(&time_mutex);
		server_time_diff = server_time_diff_global;
		pthread_mutex_unlock(&time_mutex);

		struct timespec now;
		clock_gettime(CLOCK_REALTIME, &now);
		timeadd(now, server_time_diff);
		timeadd(now, clock_period);
		timeadd(now, -delay1);
		now.tv_nsec /= clock_period;
		now.tv_nsec *= clock_period;
		timeadd(now, delay1);
		if (now.tv_sec < clock.tv_sec || (now.tv_sec == clock.tv_sec && now.tv_nsec <= clock.tv_nsec)) {
			timeadd(now, clock_period);
		}
		clock = now;
		timeadd(now, -server_time_diff);
		while (clock_nanosleep(CLOCK_REALTIME, TIMER_ABSTIME, &now, NULL) == EINTR);
		timeadd(now, server_time_diff);
		timeadd(now, delay2);

		snd_pcm_sframes_t availp, delayp;
		if (snd != NULL) {
			snd_pcm_avail_delay(snd, &availp, &delayp);
			if (delayp < -1) {
				printverbose("%d bad delayp %ld %ld, resetting\n", snd_pcm_state(snd), availp, delayp);
				snd_pcm_drop(snd);
				snd_pcm_reset(snd);
				snd_pcm_prepare(snd);
				continue;
			}
			if (snd_pcm_state(snd) == 2) {
				void *pcm2 = calloc(buffer, pcm_size_multiplier);
				if (!pcm2) {
					fprintf(stderr, "Could not allocate %lu bytes of memory!\n", buffer * pcm_size_multiplier);
					exit(1);
				}
				snd_pcm_writei(snd, pcm2, buffer);
				free(pcm2);
			}
		}

		if (verbose) {
			struct timespec now2;
			clock_gettime(CLOCK_REALTIME, &now2);
			timeadd(now2, server_time_diff);
			printverbose("%d clock %ld.%09lu now2 %ld.%09lu, avail_delay %6ld %6ld %6ld, delay %"PRId64" %"PRId64"\n", snd_pcm_state(snd), now.tv_sec, now.tv_nsec, now2.tv_sec, now2.tv_nsec, availp, delayp, availp + delayp, delay1, delay2);
		}

		pthread_mutex_lock(&audio_mutex);
		while (audio_buffer) {
			if (audio_buffer->packet.tv_sec == now.tv_sec && audio_buffer->packet.tv_nsec == now.tv_nsec) {
				printverbose("got packet %"PRIi64".%09"PRIu32"\n", audio_buffer->packet.tv_sec, audio_buffer->packet.tv_nsec);
				currframe = audio_buffer;
				audio_buffer = audio_buffer->next;
				last_packet_clock = now;
				break;
			} else if (audio_buffer->packet.tv_sec > now.tv_sec || (audio_buffer->packet.tv_sec == now.tv_sec && audio_buffer->packet.tv_nsec > now.tv_nsec)) {
				//printverbose("future packet %"PRIi64".%09"PRIu32"\n", audio_buffer->packet.tv_sec, audio_buffer->packet.tv_nsec);
				break;
			} else {
				printverbose("skipping packet %"PRIi64".%09"PRIu32"\n", audio_buffer->packet.tv_sec, audio_buffer->packet.tv_nsec);
				currframe = audio_buffer;
				audio_buffer = audio_buffer->next;
				free(currframe);
				currframe = NULL;
			}
		}
		pthread_mutex_unlock(&audio_mutex);

		int r;
		if (currframe) {
			if (use_float) {
				r = opus_decode_float(decoder, &currframe->packet.data, currframe->datalen, pcm, samples, 0);
			} else {
				r = opus_decode(decoder, &currframe->packet.data, currframe->datalen, pcm, samples, 0);
			}
			free(currframe);
		} else {
			printverbose("no packet received!\n");
			r = opus_decode(decoder, NULL, 0, pcm, samples, 1);
		}

		if (r != samples) {
			fprintf(stderr, "opus_decode: %s\n", opus_strerror(r));
			exit(1);
		}

		if (snd != NULL) {
			int retval = snd_pcm_writei(snd, pcm, samples);
			if (retval == -11) {
				printverbose("Zero write, %d < %lu\n", retval, samples);
				if (availp == samples - 1) {
					snd_pcm_prepare(snd);
				}
			} else if (retval < 0) {
				printverbose("recovering %d\n", retval);
				snd_callcheck2(snd_pcm_recover, "snd_pcm_writei", retval, snd, retval, 0);
			} else if (retval != samples) {
				printverbose("Short write, %d != %lu\n", retval, samples);
			}
		} else {
			int f = 0;
			while (f < pcm_size) {
				int f2 = write(1, (uint8_t *) pcm + f, pcm_size - f);
				if (f2 <= 0) {
					fprintf(stderr, "Error while writing audio to stdout, %d, %d %s\n", f2, errno, strerror(errno));
					exit(1);
				}
				f += f2;
			}
		}
	}

	printverbose("Audio playback thread exiting\n");

	if (snd && snd_pcm_close(snd) < 0)
		abort();

	opus_decoder_destroy(decoder);

	pthread_mutex_destroy(&audio_mutex);
	pthread_mutex_destroy(&time_mutex);
	pthread_exit(NULL);
	return NULL;
}

int main(int argc, char *argv[]) {
	fprintf(stderr, "mrx - Receive audio via UDP unicast or multicast\n");
	fprintf(stderr, "Copyright (C) 2014-2017 Vittorio Gambaletta <openwrt@vittgam.net>\n\n");

	while (1) {
		int c = getopt(argc, argv, "h:p:d:f:r:c:t:b:e:T:v:");
		if (c == -1) {
			break;
		} else if (c == 'h') {
			addr = optarg;
		} else if (c == 'p') {
			port = strtoul(optarg, NULL, 10);
		} else if (c == 'd') {
			device = optarg;
		} else if (c == 'f') {
			use_float = strtoul(optarg, NULL, 10);
		} else if (c == 'r') {
			rate = strtoul(optarg, NULL, 10);
		} else if (c == 'c') {
			channels = strtoul(optarg, NULL, 10);
		} else if (c == 't') {
			audio_packet_duration = strtoul(optarg, NULL, 10);
		} else if (c == 'b') {
			buffermult = strtoul(optarg, NULL, 10);
		} else if (c == 'e') {
			delay = strtol(optarg, NULL, 10);
		} else if (c == 'T') {
			enable_time_sync = strtoul(optarg, NULL, 10);
		} else if (c == 'v') {
			verbose = strtoul(optarg, NULL, 10);
		} else {
			fprintf(stderr, "\nUsage: mrx [<options>]\n\n");
			fprintf(stderr, "    -h <addr>   IP address (default: %s)\n", addr);
			fprintf(stderr, "    -p <port>   UDP port (default: %lu)\n", port);
			fprintf(stderr, "    -d <dev>    ALSA device name, or '-' for stdin/stdout (default: '%s')\n", device);
			fprintf(stderr, "    -f <n>      Use float samples (1) or signed 16 bit integer samples (0) (default: %lu)\n", use_float);
			fprintf(stderr, "    -r <rate>   Audio sample rate (default: %lu Hz)\n", rate);
			fprintf(stderr, "    -c <n>      Channels count (default: %lu)\n", channels);
			fprintf(stderr, "    -t <ms>     Audio packet duration (default: %lu ms)\n", audio_packet_duration);
			fprintf(stderr, "    -b <n>      ALSA buffer multiplier (default: %lu)\n", buffermult);
			fprintf(stderr, "    -e <ms>     Audio total delay (default: %ld ms)\n", delay);
			fprintf(stderr, "    -T <n>      Enable or disable time synchronization (default: %lu)\n", enable_time_sync);
			fprintf(stderr, "    -v <n>      Be verbose (default: %lu)\n", verbose);
			fprintf(stderr, "\n");
			exit(1);
		}
	}

	int sock = init_socket(1);

	set_realtime_prio();

	pthread_barrier_init(&init_barrier, NULL, 2);

	int ret;
	pthread_t ths1;
	pthread_attr_t thattr1;

	pthread_mutex_init(&audio_mutex, NULL);
	pthread_mutex_init(&time_mutex, NULL);
	pthread_attr_init(&thattr1);
	pthread_attr_setdetachstate(&thattr1, PTHREAD_CREATE_DETACHED);
	if ((ret = pthread_create(&ths1, &thattr1, audio_playback_thread, NULL)) != 0) {
		fprintf(stderr, "Error while calling pthread_create() for audio playback thread: error %d (%s)\n", ret, strerror(ret));
		exit(1);
	}
	pthread_attr_destroy(&thattr1);

	pthread_barrier_wait(&init_barrier);
	pthread_barrier_destroy(&init_barrier);

	drop_privs_if_needed();

	struct timespec last_time_sent;
	memset(&last_time_sent, 0, sizeof(last_time_sent));

	while (1) {
		struct sockaddr_in addrin;
		unsigned int addrinlen = sizeof(addrin);
		memset(&addrin, 0, sizeof(addrin));

		errno = 0;
		int plen = recv(sock, NULL, 0, MSG_PEEK | MSG_TRUNC);
		if (errno == EFAULT && ioctl(sock, FIONREAD, &plen)) {
			perror("ioctl(FIONREAD)");
			exit(1);
		}
		if (plen <= 0 || plen <= sizeof(struct timep)) {
			perror("recv(PEEK)");
			exit(1);
		}

		struct azz *currframe = malloc(offsetof(struct azz, packet) + plen);
		if (!currframe) {
			fprintf(stderr, "Could not allocate %lu bytes of memory!\n", (unsigned long int) offsetof(struct azz, packet) + plen);
			exit(1);
		}

		errno = 0;
		currframe->datalen = recvfrom(sock, &currframe->packet, plen, 0, (struct sockaddr *) &addrin, &addrinlen);
		if (currframe->datalen != plen || addrinlen != sizeof(addrin)) {
			perror("recvfrom");
			free(currframe);
			exit(1);
		}

		struct timespec time_recv;
		clock_gettime(CLOCK_REALTIME, &time_recv);

		currframe->datalen -= sizeof(struct timep);
		currframe->packet.tv_sec = be64toh(currframe->packet.tv_sec);
		currframe->packet.tv_nsec = be32toh(currframe->packet.tv_nsec);

		if (currframe->datalen == sizeof(struct timep)) {
			struct timep2 *timepacket = (struct timep2 *)&currframe->packet;
			if (last_time_sent.tv_sec != 0 && timepacket->t1.tv_sec == last_time_sent.tv_sec && timepacket->t1.tv_nsec == last_time_sent.tv_nsec) {
				struct timespec time_serv;
				time_serv.tv_sec = be64toh(timepacket->t2.tv_sec);
				time_serv.tv_nsec = be32toh(timepacket->t2.tv_nsec);

				int64_t server_time_diff = (int64_t)time_serv.tv_sec - (((int64_t)last_time_sent.tv_sec + (int64_t)time_recv.tv_sec) / 2);
				server_time_diff *= 1000000000;
				server_time_diff += (int64_t)time_serv.tv_nsec - (((int64_t)last_time_sent.tv_nsec + (int64_t)time_recv.tv_nsec) / 2);

				pthread_mutex_lock(&time_mutex);
				server_time_diff_global = server_time_diff;
				pthread_mutex_unlock(&time_mutex);

				printverbose("Time packet received! sent = %ld.%09lu, serv = %ld.%09lu, recv = %ld.%09lu, diff = %+011"PRIi64"\n", last_time_sent.tv_sec, last_time_sent.tv_nsec, time_serv.tv_sec, time_serv.tv_nsec, time_recv.tv_sec, time_recv.tv_nsec, server_time_diff);
			} else {
				fprintf(stderr, "Invalid time packet received!\n");
			}
			continue;
		} else if (enable_time_sync && last_time_sent.tv_sec != time_recv.tv_sec) {
			struct timep timepacket;
			clock_gettime(CLOCK_REALTIME, &last_time_sent);
			timepacket.tv_sec = htobe64(last_time_sent.tv_sec);
			timepacket.tv_nsec = htobe32(last_time_sent.tv_nsec);
			if (sendto(sock, &timepacket, sizeof(struct timep), 0, (struct sockaddr *) &addrin, sizeof(addrin)) < 0) {
				perror("sendto");
			}
		}

		pthread_mutex_lock(&audio_mutex);
		if (audio_buffer && ((currframe->packet.tv_sec < audio_buffer->packet.tv_sec) || (currframe->packet.tv_sec == audio_buffer->packet.tv_sec && currframe->packet.tv_nsec < audio_buffer->packet.tv_nsec)) && ((currframe->packet.tv_sec <= last_packet_clock.tv_sec) || (currframe->packet.tv_sec == last_packet_clock.tv_sec && currframe->packet.tv_nsec <= last_packet_clock.tv_nsec))) {
			fprintf(stderr, "Received frame %"PRIi64".%09"PRIu32" in the past (current = %"PRIi64".%09"PRIu32")\n", currframe->packet.tv_sec, currframe->packet.tv_nsec, audio_buffer->packet.tv_sec, audio_buffer->packet.tv_nsec);
			free(currframe);
		} else {
			struct azz **audio_buffer_ptr = &audio_buffer;
			unsigned int counter = delay < 150 ? 50 : (delay / 3);
			while (--counter && *audio_buffer_ptr && (((*audio_buffer_ptr)->packet.tv_sec < currframe->packet.tv_sec) || ((*audio_buffer_ptr)->packet.tv_sec == currframe->packet.tv_sec && (*audio_buffer_ptr)->packet.tv_nsec < currframe->packet.tv_nsec))) {
				audio_buffer_ptr = &(*audio_buffer_ptr)->next;
			}
			if (!counter) {
				fprintf(stderr, "Received frame %"PRIi64".%09"PRIu32" too far in the future (current = %"PRIi64".%09"PRIu32")\n", currframe->packet.tv_sec, currframe->packet.tv_nsec, audio_buffer->packet.tv_sec, audio_buffer->packet.tv_nsec);
				while (audio_buffer) {
					struct azz *tmp = audio_buffer;
					audio_buffer = audio_buffer->next;
					free(tmp);
				}
				currframe->next = NULL;
				audio_buffer = currframe;
			} else if (*audio_buffer_ptr && (*audio_buffer_ptr)->packet.tv_sec == currframe->packet.tv_sec && (*audio_buffer_ptr)->packet.tv_nsec == currframe->packet.tv_nsec) {
				fprintf(stderr, "Received duplicated frame %"PRIi64".%09"PRIu32"\n", currframe->packet.tv_sec, currframe->packet.tv_nsec);
				free(currframe);
			} else {
				currframe->next = *audio_buffer_ptr;
				*audio_buffer_ptr = currframe;
			}
		}
		pthread_mutex_unlock(&audio_mutex);
	}

	return 0;
}
