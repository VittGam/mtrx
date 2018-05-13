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

static unsigned long int kbps = 128;
static unsigned long int use_rtp = 0;

static void *time_sync_thread(void *arg) {
	printverbose("Time sync thread started\n");

	int sock = *(int *)arg;

	while (1) {
		struct timep2 timepacket;
		struct sockaddr_in addrin;
		unsigned int addrinlen = sizeof(addrin);
		memset(&addrin, 0, sizeof(addrin));

		errno = 0;
		int plen = recvfrom(sock, &timepacket, sizeof(struct timep), 0, (struct sockaddr *) &addrin, &addrinlen);
		if (plen != sizeof(struct timep) || addrinlen != sizeof(addrin)) {
			perror("recvfrom");
			continue;
		}

		struct timespec now;
		clock_gettime(CLOCK_REALTIME, &now);
		timepacket.t2.tv_sec = htobe64(now.tv_sec);
		timepacket.t2.tv_nsec = htobe32(now.tv_nsec);

		if (sendto(sock, &timepacket, sizeof(struct timep2), 0, (struct sockaddr *) &addrin, sizeof(addrin)) < 0) {
			perror("sendto");
		}
	}

	pthread_exit(NULL);
	return NULL;
}

int main(int argc, char *argv[]) {
	fprintf(stderr, "mtx - Transmit audio via UDP unicast or multicast\n");
	fprintf(stderr, "Copyright (C) 2014-2017 Vittorio Gambaletta <openwrt@vittgam.net>\n\n");

	while (1) {
		int c = getopt(argc, argv, "h:p:d:f:r:c:t:k:b:v:T:R:");
		if (c == -1) {
			break;
		} else if (c == 'h') {
			addr = optarg;
		} else if (c == 'p') {
			port = strtoul(optarg, NULL, 10);
		} else if (c == 'd') {
			device = optarg;
		} else if (c == 'R') {
			use_rtp = strtoul(optarg, NULL, 10);
		} else if (c == 'f') {
			use_float = strtoul(optarg, NULL, 10);
		} else if (c == 'r') {
			rate = strtoul(optarg, NULL, 10);
		} else if (c == 'c') {
			channels = strtoul(optarg, NULL, 10);
		} else if (c == 't') {
			audio_packet_duration = strtoul(optarg, NULL, 10);
		} else if (c == 'k') {
			kbps = strtoul(optarg, NULL, 10);
		} else if (c == 'b') {
			buffermult = strtoul(optarg, NULL, 10);
		} else if (c == 'T') {
			enable_time_sync = strtoul(optarg, NULL, 10);
		} else if (c == 'v') {
			verbose = strtoul(optarg, NULL, 10);
		} else {
			fprintf(stderr, "\nUsage: mtx [<options>]\n\n");
			fprintf(stderr, "    -h <addr>   IP address (default: %s)\n", addr);
			fprintf(stderr, "    -p <port>   UDP port (default: %lu)\n", port);
			fprintf(stderr, "    -d <dev>    ALSA device name, or '-' for stdin (default: '%s')\n", device);
			fprintf(stderr, "    -R <n>      RTP output (default: %lu)\n", use_rtp);
			fprintf(stderr, "    -f <n>      Use float samples (1) or signed 16 bit integer samples (0) (default: %lu)\n", use_float);
			fprintf(stderr, "    -r <rate>   Audio sample rate (default: %lu Hz)\n", rate);
			fprintf(stderr, "    -c <n>      Audio channel count (default: %lu)\n", channels);
			fprintf(stderr, "    -t <ms>     Audio packet duration (default: %lu ms)\n", audio_packet_duration);
			fprintf(stderr, "    -k <kbps>   Network bitrate (default: %lu kbps)\n", kbps);
			fprintf(stderr, "    -b <n>      ALSA buffer multiplier (default: %lu)\n", buffermult);
			fprintf(stderr, "    -T <n>      Enable or disable time synchronization (default: %lu)\n", enable_time_sync);
			fprintf(stderr, "    -v <n>      Be verbose (default: %lu)\n", verbose);
			fprintf(stderr, "\n");
			exit(1);
		}
	}

	if (use_rtp && enable_time_sync) {
		fprintf(stderr, "Disabling time synchronization due to RTP output.\n");
		enable_time_sync = 0;
	}

	int sock = init_socket(0);

	set_realtime_prio();

	if (enable_time_sync) {
		int ret;
		pthread_t ths1;
		pthread_attr_t thattr1;
		pthread_attr_init(&thattr1);
		pthread_attr_setdetachstate(&thattr1, PTHREAD_CREATE_DETACHED);
		if ((ret = pthread_create(&ths1, &thattr1, time_sync_thread, (void *)&sock)) != 0) {
			fprintf(stderr, "Error while calling pthread_create() for time sync thread: error %d (%s)\n", ret, strerror(ret));
			exit(1);
		}
		pthread_attr_destroy(&thattr1);
	}

	snd_pcm_uframes_t samples = audio_packet_duration * rate / 1000;
	size_t pcm_size_multiplier = (use_float ? sizeof(float) : sizeof(int16_t)) * channels;
	size_t pcm_size = samples * pcm_size_multiplier;
	uint64_t clock_period = (uint64_t) 1000000 * audio_packet_duration;
	size_t bytes_per_frame = kbps * audio_packet_duration / 8;

	int error;
	OpusEncoder *encoder = opus_encoder_create(rate, channels, OPUS_APPLICATION_AUDIO, &error);
	if (encoder == NULL) {
		fprintf(stderr, "opus_encoder_create: %s\n", opus_strerror(error));
		exit(1);
	}
	opus_encoder_ctl(encoder, OPUS_SET_BITRATE(kbps * 1000));
	opus_encoder_ctl(encoder, OPUS_SET_COMPLEXITY(9));

	snd_pcm_t *snd = NULL;
	snd_pcm_uframes_t buffer = samples;
	if (strcmp(device, "-") != 0) {
		snd = snd_my_init(device, SND_PCM_STREAM_CAPTURE, rate, channels, use_float, &buffer, buffermult);
	}

	void *pcm = alloca(pcm_size);
	struct azzp *azzp_packet = NULL;
	struct rtp *rtp_packet = NULL;
	if (use_rtp) {
		rtp_packet = (struct rtp *)alloca(bytes_per_frame + sizeof(struct rtp) - 1);
	} else {
		azzp_packet = (struct azzp *)alloca(bytes_per_frame + sizeof(struct azzp) - 1);
	}

	struct timespec clock = {0, 0};
	int resync = 1;

	drop_privs_if_needed();

	// Used for RTP only.
	srand(time(NULL));
	uint16_t rtp_seq = rand();
	uint32_t ssrc = rand();
	uint32_t ts = 0;

	if (use_rtp) {
		printf("SDP file for this stream:\n\n");
		printf("v=0\n");
		printf("o=- 0 0 IN IP4 127.0.0.1\n");
		printf("s=No Name\n");
		printf("c=IN IP4 %s\n", addr);
		printf("t=0 0\n");
		printf("a=tool:mtx\n");
		printf("m=audio %ld RTP/AVP 96\n", port);
		printf("b=AS:96\n");
		printf("a=rtpmap:96 opus/48000/%lu\n", channels);
		printf("a=fmtp:96 sprop-stereo=%d\n", channels > 1);
		printf("a=control:streamid=0\n\n");
	}

	while (1) {
		printverbose("clock %ld.%09lu\n", clock.tv_sec, clock.tv_nsec);

		if (snd != NULL) {
			// one of the many ways alsa-pulse is broken, is that audio sometimes glitches if snd_pcm_avail_delay is polled continuously...
			if (resync) {
				resync = 0;
				snd_pcm_sframes_t availp, delayp;
				snd_pcm_avail_delay(snd, &availp, &delayp);
				printverbose("availp %ld %ld / %ld %ld\n", buffer, samples, availp, delayp);
				// one of the many ways alsa-pulse is broken, is that capture buffer overrun is not properly detected and reported...
				if (delayp < 0) {
					resync = 1;
				} else if (delayp > buffer) {
					// one of the many ways alsa-pulse is broken, is that it does not implement snd_pcm_reset...
					while (delayp > samples) {
						printverbose("%d bad delayp %ld %ld, draining\n", snd_pcm_state(snd), availp, delayp);
						snd_pcm_sframes_t delayp2 = (availp > samples && availp < delayp) ? availp : delayp;
						if (delayp2 > 100000) {
							delayp2 = 100000;
						}
						void *pcm2 = calloc(delayp2, pcm_size_multiplier);
						if (!pcm2) {
							fprintf(stderr, "Could not allocate %lu bytes of memory!\n", delayp2 * pcm_size_multiplier);
							exit(1);
						}
						int ret = snd_pcm_readi(snd, pcm2, delayp2);
						snd_pcm_avail_delay(snd, &availp, &delayp);
						free(pcm2);
						printverbose("drained %d of %ld, new %ld %ld\n", ret, delayp2, availp, delayp);
						if (ret != delayp2) {
							break;
						}
					}
				}
			}

			int f = snd_pcm_readi(snd, pcm, samples);
			if (f < 0) {
				fprintf(stderr, "Recovering from error %d\n", f);
				snd_callcheck2(snd_pcm_recover, "snd_pcm_readi", f, snd, f, 0);
				continue;
			} else if (f != samples) {
				fprintf(stderr, "Short read, %d < %lu\n", f, samples);
				continue;
			}
		} else {
			int f = 0;
			while (f < pcm_size) {
				int f2 = read(0, (uint8_t *) pcm + f, pcm_size - f);
				if (f2 <= 0) {
					fprintf(stderr, "Error while reading audio from stdin, %d, %d %s\n", f2, errno, strerror(errno));
					exit(1);
				}
				f += f2;
			}
		}

		unsigned char *data = use_rtp ? &rtp_packet->data : &azzp_packet->data;

		ssize_t z;
		if (use_float) {
			z = opus_encode_float(encoder, pcm, samples, data, bytes_per_frame);
		} else {
			z = opus_encode(encoder, pcm, samples, data, bytes_per_frame);
		}
		if (z < 0) {
			fprintf(stderr, "opus_encode: %s\n", opus_strerror(z));
			exit(1);
		}

		struct sockaddr_in addrin;
		memset(&addrin, 0, sizeof(addrin));
		addrin.sin_family = AF_INET;
		addrin.sin_addr.s_addr = inet_addr(addr);
		addrin.sin_port = htons((uint16_t) port);

		struct timespec now;
		clock_gettime(CLOCK_REALTIME, &now);
		now.tv_nsec /= clock_period;
		now.tv_nsec *= clock_period;
		if (now.tv_sec < clock.tv_sec || (now.tv_sec == clock.tv_sec && now.tv_nsec <= clock.tv_nsec)) {
			timeadd(now, clock_period);
			while (clock_nanosleep(CLOCK_REALTIME, TIMER_ABSTIME, &now, NULL) == EINTR);
		}
		// resync every 5 seconds or when there's a delay bigger than clock_period (eg. after SIGSTOP/SIGCONT)
		if (clock.tv_sec && ((now.tv_sec % 5 == 0 && now.tv_nsec == 0) || ((((1000000000LL * (now.tv_sec - clock.tv_sec)) + (now.tv_nsec - clock.tv_nsec))) > clock_period))) {
			resync = 1;
		}
		printverbose("resync %lld %d\n", (((1000000000LL * (now.tv_sec - clock.tv_sec)) + (now.tv_nsec - clock.tv_nsec))), resync);
		clock = now;

		if (use_rtp) {
			rtp_packet->version_p_x_cc = (2 << 6);  // Version 2, the other fields are zero.
			rtp_packet->marker_and_pt = 96;  // First ID that's dynamically allocated.
			rtp_packet->seq = htobe16(rtp_seq++);
			rtp_packet->timestamp = htobe32(ts);
			rtp_packet->ssrc = htobe32(ssrc);
			ts += samples;

			if (sendto(sock, rtp_packet, z + sizeof(struct rtp) - 1, 0, (struct sockaddr *) &addrin, sizeof(addrin)) < 0) {
				perror("sendto");
				exit(1);
			}
		} else {
			azzp_packet->tv_sec = htobe64(now.tv_sec);
			azzp_packet->tv_nsec = htobe32(now.tv_nsec);

			if (sendto(sock, azzp_packet, z + sizeof(struct azzp) - 1, 0, (struct sockaddr *) &addrin, sizeof(addrin)) < 0) {
				perror("sendto");
				exit(1);
			}
		}
	}

	if (snd && snd_pcm_close(snd) < 0)
		abort();

	opus_encoder_destroy(encoder);

	return 0;
}
