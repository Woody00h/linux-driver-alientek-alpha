// SPDX-License-Identifier: GPL-2.0
/* Direct ALSA PCM UAPI, no libasound dependency. Raw S16_LE/48kHz/stereo. */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <math.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>
#include <sound/asound.h>

#define RATE 48000U
#define PERIOD 1024U
#define BUFFER (PERIOD * 4U)
#define FRAME_BYTES 4U
#define CAPTURE_WARMUP_FRAMES (3U * RATE)
static volatile sig_atomic_t stopped;

static void stop_handler(int sig)
{
	(void)sig;
	stopped = 1;
}

static void interval(struct snd_pcm_hw_params *p, unsigned int key,
		     unsigned int value)
{
	struct snd_interval *i = &p->intervals[key - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL];
	i->min = i->max = value;
	i->integer = 1;
}

static void mask(struct snd_pcm_hw_params *p, unsigned int key, unsigned int bit)
{
	struct snd_mask *m = &p->masks[key - SNDRV_PCM_HW_PARAM_FIRST_MASK];
	memset(m, 0, sizeof(*m));
	m->bits[bit / 32] = 1U << (bit % 32);
}

static int pcm_open(const char *path, int capture)
{
	struct snd_pcm_hw_params hw = {0};
	struct snd_pcm_sw_params sw = {0};
	int fd = open(path, O_RDWR | O_CLOEXEC);
	unsigned int i, j;
	int saved;

	if (fd < 0)
		return -1;
	for (i = 0; i < sizeof(hw.masks) / sizeof(hw.masks[0]); i++)
		for (j = 0; j < sizeof(hw.masks[i].bits) / sizeof(hw.masks[i].bits[0]); j++)
			hw.masks[i].bits[j] = UINT_MAX;
	for (i = 0; i < sizeof(hw.intervals) / sizeof(hw.intervals[0]); i++)
		hw.intervals[i].max = UINT_MAX;
	hw.rmask = UINT_MAX;
	mask(&hw, SNDRV_PCM_HW_PARAM_ACCESS, SNDRV_PCM_ACCESS_RW_INTERLEAVED);
	mask(&hw, SNDRV_PCM_HW_PARAM_FORMAT, SNDRV_PCM_FORMAT_S16_LE);
	mask(&hw, SNDRV_PCM_HW_PARAM_SUBFORMAT, SNDRV_PCM_SUBFORMAT_STD);
	interval(&hw, SNDRV_PCM_HW_PARAM_CHANNELS, 2);
	interval(&hw, SNDRV_PCM_HW_PARAM_RATE, RATE);
	interval(&hw, SNDRV_PCM_HW_PARAM_PERIOD_SIZE, PERIOD);
	interval(&hw, SNDRV_PCM_HW_PARAM_BUFFER_SIZE, BUFFER);
	if (ioctl(fd, SNDRV_PCM_IOCTL_HW_PARAMS, &hw) < 0)
		goto fail;
	sw.period_step = 1;
	sw.avail_min = PERIOD;
	sw.xfer_align = 1;
	sw.start_threshold = capture ? 1 : PERIOD;
	sw.stop_threshold = BUFFER;
	sw.boundary = BUFFER;
	while (sw.boundary <= (snd_pcm_uframes_t)LONG_MAX / 2 - BUFFER)
		sw.boundary *= 2;
	if (ioctl(fd, SNDRV_PCM_IOCTL_SW_PARAMS, &sw) < 0 ||
	    ioctl(fd, SNDRV_PCM_IOCTL_PREPARE) < 0)
		goto fail;
	return fd;
fail:
	saved = errno;
	close(fd);
	errno = saved;
	return -1;
}

static int transfer(int fd, void *data, unsigned int frames, int capture)
{
	unsigned int done = 0;

	while (done < frames && !stopped) {
		struct snd_xferi x = {
			.buf = (unsigned char *)data + done * FRAME_BYTES,
			.frames = frames - done,
		};
		int rc = ioctl(fd, capture ? SNDRV_PCM_IOCTL_READI_FRAMES :
			       SNDRV_PCM_IOCTL_WRITEI_FRAMES, &x);
		int err = rc < 0 ? errno : (x.result < 0 ? (int)-x.result : 0);

		if (err == EINTR)
			continue;
		/* Fail visibly on XRUN: a successful test must contain no gaps. */
		if (err) {
			errno = err;
			return -1;
		}
		if (x.result <= 0 || (snd_pcm_uframes_t)x.result > x.frames) {
			errno = EIO;
			return -1;
		}
		done += (unsigned int)x.result;
	}
	return (int)done;
}

static void usage(const char *name)
{
	fprintf(stderr,
		"Usage:\n  %s tone DEVICE SECONDS [HZ [left|right|both]]\n"
		"  %s record DEVICE FILE.pcm SECONDS\n"
		"  %s play DEVICE FILE.pcm\n"
		"DEVICE: /dev/snd/pcmC0D0p for output, pcmC0D0c for input\n"
		"Format: raw S16_LE, 48000 Hz, stereo. Tone amplitude: 10%%.\n",
		name, name, name);
}

static int number(const char *s, unsigned int min, unsigned int max,
		  unsigned int *out)
{
	char *end;
	unsigned long v;

	errno = 0;
	v = strtoul(s, &end, 10);
	if (errno || end == s || *end || v < min || v > max)
		return -1;
	*out = (unsigned int)v;
	return 0;
}

int main(int argc, char **argv)
{
	unsigned char data[PERIOD * FRAME_BYTES];
	unsigned int seconds = 0, hz = 1000;
	uint64_t remaining = 0, position = 0;
	struct timespec start, end;
	struct sigaction sa = { .sa_handler = stop_handler };
	FILE *file = NULL;
	int capture, tone, fd = -1, result = EXIT_FAILURE;
	const char *channel = "both";

	if (argc < 2)
		goto bad_args;
	capture = !strcmp(argv[1], "record");
	tone = !strcmp(argv[1], "tone");
	if (tone) {
		if (argc < 4 || argc > 6 || number(argv[3], 1, 3600, &seconds))
			goto bad_args;
		if (argc >= 5 && number(argv[4], 20, 20000, &hz))
			goto bad_args;
		if (argc == 6)
			channel = argv[5];
		if (strcmp(channel, "left") && strcmp(channel, "right") && strcmp(channel, "both"))
			goto bad_args;
	} else if (capture) {
		if (argc != 5 || number(argv[4], 1, 3600, &seconds))
			goto bad_args;
	} else if (strcmp(argv[1], "play") || argc != 4) {
		goto bad_args;
	}
	sigemptyset(&sa.sa_mask);
	if (sigaction(SIGINT, &sa, NULL) || sigaction(SIGTERM, &sa, NULL)) {
		perror("sigaction");
		return EXIT_FAILURE;
	}
	fd = pcm_open(argv[2], capture);
	if (fd < 0) {
		perror("PCM open/configure (48kHz S16_LE stereo)");
		goto out;
	}
	if (!tone) {
		/* Exclusive creation prevents accidental overwrite of a recording. */
		file = fopen(argv[3], capture ? "wbx" : "rb");
		if (!file) {
			perror("PCM file");
			goto out;
		}
	}
	remaining = (uint64_t)seconds * RATE;
	if (capture) {
		unsigned int warmup = CAPTURE_WARMUP_FRAMES;

		printf("Microphone warm-up: discarding 3 seconds of samples...\n");
		fflush(stdout);
		while (warmup && !stopped) {
			unsigned int frames = warmup < PERIOD ? warmup : PERIOD;
			int n = transfer(fd, data, frames, 1);

			if (n < 0) {
				perror("PCM warm-up");
				goto out;
			}
			warmup -= (unsigned int)n;
		}
		if (stopped) {
			result = 130;
			goto out;
		}
		printf("RECORDING NOW: speak continuously for %u seconds.\n", seconds);
		fflush(stdout);
	}
	if (clock_gettime(CLOCK_MONOTONIC, &start) < 0) {
		perror("clock_gettime");
		goto out;
	}
	while (!stopped) {
		unsigned int frames = PERIOD, i;
		int transferred;

		if (tone || capture) {
			if (!remaining)
				break;
			if (remaining < frames)
				frames = (unsigned int)remaining;
		} else {
			size_t bytes = fread(data, 1, sizeof(data), file);
			if (ferror(file)) {
				perror("read file");
				goto out;
			}
			if (bytes % FRAME_BYTES) {
				fprintf(stderr, "PCM file ends with an incomplete stereo frame\n");
				goto out;
			}
			frames = (unsigned int)(bytes / FRAME_BYTES);
			if (!frames)
				break;
		}
		if (tone) {
			for (i = 0; i < frames; i++) {
				int16_t sample = (int16_t)(3276.0 * sin(6.283185307179586 *
					 hz * (double)((position + i) % RATE) / RATE));
				uint16_t left = !strcmp(channel, "right") ? 0 : (uint16_t)sample;
				uint16_t right = !strcmp(channel, "left") ? 0 : (uint16_t)sample;
				data[i * 4] = left & 255;
				data[i * 4 + 1] = left >> 8;
				data[i * 4 + 2] = right & 255;
				data[i * 4 + 3] = right >> 8;
			}
		}
		transferred = transfer(fd, data, frames, capture);
		if (transferred < 0) {
			perror("PCM transfer (EPIPE means underrun/overrun)");
			goto out;
		}
		if (capture && fwrite(data, FRAME_BYTES, (size_t)transferred, file) !=
		    (size_t)transferred) {
			perror("write recording");
			goto out;
		}
		position += (unsigned int)transferred;
		if (tone || capture)
			remaining -= (unsigned int)transferred;
	}
	if (!capture && !stopped && ioctl(fd, SNDRV_PCM_IOCTL_DRAIN) < 0) {
		perror("PCM drain");
		goto out;
	}
	if (clock_gettime(CLOCK_MONOTONIC, &end) < 0) {
		perror("clock_gettime");
		goto out;
	}
	printf("%s: %llu frames, %.3f seconds of audio, %.3f seconds elapsed%s\n",
	       capture ? "Captured" : "Played",
	       (unsigned long long)position, (double)position / RATE,
	       (double)(end.tv_sec - start.tv_sec) +
	       (double)(end.tv_nsec - start.tv_nsec) / 1000000000.0,
	       stopped ? " (interrupted)" : "");
	result = stopped ? 130 : EXIT_SUCCESS;
out:
	if (file && fclose(file)) {
		perror("close file");
		result = EXIT_FAILURE;
	}
	if (fd >= 0) {
		ioctl(fd, SNDRV_PCM_IOCTL_DROP);
		close(fd);
	}
	return result;
bad_args:
	usage(argv[0]);
	return EXIT_FAILURE;
}
