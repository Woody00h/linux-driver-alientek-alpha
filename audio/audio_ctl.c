// SPDX-License-Identifier: GPL-2.0
/* Minimal ALSA mixer writer for BusyBox systems without alsa-utils. */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <sound/asound.h>

static int parse_long(const char *s, long min, long max, long *value)
{
	char *end;
	long n;

	errno = 0;
	n = strtol(s, &end, 10);
	if (errno || end == s || *end || n < min || n > max) {
		errno = EINVAL;
		return -1;
	}
	*value = n;
	return 0;
}

static int parse_value(int fd, const struct snd_ctl_elem_info *info,
		       const char *s, long *value)
{
	unsigned int i;

	switch (info->type) {
	case SNDRV_CTL_ELEM_TYPE_BOOLEAN:
		if (!strcasecmp(s, "on")) {
			*value = 1;
			return 0;
		}
		if (!strcasecmp(s, "off")) {
			*value = 0;
			return 0;
		}
		return parse_long(s, 0, 1, value);
	case SNDRV_CTL_ELEM_TYPE_INTEGER:
		return parse_long(s, info->value.integer.min,
				  info->value.integer.max, value);
	case SNDRV_CTL_ELEM_TYPE_ENUMERATED:
		for (i = 0; i < info->value.enumerated.items; i++) {
			struct snd_ctl_elem_info item = { .id = info->id };

			item.value.enumerated.item = i;
			if (ioctl(fd, SNDRV_CTL_IOCTL_ELEM_INFO, &item) < 0)
				return -1;
			if (!strcmp(s, item.value.enumerated.name)) {
				*value = i;
				return 0;
			}
		}
		if (info->value.enumerated.items)
			return parse_long(s, 0, info->value.enumerated.items - 1, value);
		break;
	default:
		errno = ENOTSUP;
		return -1;
	}
	errno = EINVAL;
	return -1;
}

int main(int argc, char **argv)
{
	struct snd_ctl_elem_info info = {0};
	struct snd_ctl_elem_value value = {0}, actual = {0};
	long card, parsed[128];
	char path[64], text[512], *token, *comma;
	unsigned int count = 0, i;
	int fd, result = EXIT_FAILURE;

	if (argc != 4 || parse_long(argv[1], 0, 255, &card) ||
	    !argv[2][0] || strlen(argv[2]) >= sizeof(info.id.name) ||
	    strlen(argv[3]) >= sizeof(text)) {
		fprintf(stderr, "Usage: %s CARD 'CONTROL NAME' 'VALUE[,VALUE]'\n"
			"One value applies to all channels; supports on/off, integers, enums.\n",
			argv[0]);
		return EXIT_FAILURE;
	}
	snprintf(path, sizeof(path), "/dev/snd/controlC%ld", card);
	fd = open(path, O_RDWR | O_CLOEXEC);
	if (fd < 0) {
		perror(path);
		return EXIT_FAILURE;
	}
	info.id.iface = SNDRV_CTL_ELEM_IFACE_MIXER;
	memcpy(info.id.name, argv[2], strlen(argv[2]) + 1);
	if (ioctl(fd, SNDRV_CTL_IOCTL_ELEM_INFO, &info) < 0) {
		perror("control lookup");
		goto out;
	}
	if (!(info.access & SNDRV_CTL_ELEM_ACCESS_WRITE) ||
	    !info.count || info.count > 128) {
		fprintf(stderr, "Control is not writable or has unsupported channel count\n");
		goto out;
	}
	strcpy(text, argv[3]);
	token = text;
	do {
		comma = strchr(token, ',');
		if (comma)
			*comma = '\0';
		if (count >= info.count || parse_value(fd, &info, token, &parsed[count])) {
			fprintf(stderr, "Invalid value or too many channels: %s\n", argv[3]);
			goto out;
		}
		count++;
		if (comma)
			token = comma + 1;
	} while (comma);
	if (count != 1 && count != info.count) {
		fprintf(stderr, "Expected 1 or %u values, received %u\n", info.count, count);
		goto out;
	}
	value.id = info.id;
	for (i = 0; i < info.count; i++) {
		long n = parsed[count == 1 ? 0 : i];

		if (info.type == SNDRV_CTL_ELEM_TYPE_ENUMERATED)
			value.value.enumerated.item[i] = (unsigned int)n;
		else
			value.value.integer.value[i] = n;
	}
	if (ioctl(fd, SNDRV_CTL_IOCTL_ELEM_WRITE, &value) < 0) {
		perror("control write");
		goto out;
	}
	actual.id = info.id;
	if (ioctl(fd, SNDRV_CTL_IOCTL_ELEM_READ, &actual) < 0) {
		perror("control readback");
		goto out;
	}
	for (i = 0; i < info.count; i++) {
		long n = info.type == SNDRV_CTL_ELEM_TYPE_ENUMERATED ?
			(long)actual.value.enumerated.item[i] : actual.value.integer.value[i];

		if (n != parsed[count == 1 ? 0 : i]) {
			fprintf(stderr, "Control readback mismatch on channel %u\n", i);
			goto out;
		}
	}
	result = EXIT_SUCCESS;
out:
	if (result != EXIT_SUCCESS)
		fprintf(stderr, "Failed: card %ld, %s = %s\n", card, argv[2], argv[3]);
	close(fd);
	return result;
}
