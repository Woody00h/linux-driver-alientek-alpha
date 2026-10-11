// SPDX-License-Identifier: GPL-2.0
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t stopped;

static void stop_handler(int sig)
{
	(void)sig;
	stopped = 1;
}

static int write_attr(const char *dir, const char *attr, const char *value)
{
	char path[PATH_MAX];
	size_t len = strlen(value);
	ssize_t n;
	int fd, saved;

	if (snprintf(path, sizeof(path), "%s/%s", dir, attr) >= (int)sizeof(path)) {
		errno = ENAMETOOLONG;
		return -1;
	}
	fd = open(path, O_WRONLY | O_CLOEXEC);
	if (fd < 0)
		return -1;
	do {
		n = write(fd, value, len);
	} while (n < 0 && errno == EINTR);
	saved = errno;
	if (n >= 0 && (size_t)n != len)
		saved = EIO;
	if (close(fd) < 0 && n == (ssize_t)len)
		return -1;
	if (n != (ssize_t)len) {
		errno = saved;
		return -1;
	}
	return 0;
}

static int number(const char *s, unsigned long max, unsigned long *out)
{
	char *end;
	unsigned long n;

	if (*s < '0' || *s > '9')
		return -1;
	errno = 0;
	n = strtoul(s, &end, 10);
	if (errno || *end || !n || n > max)
		return -1;
	*out = n;
	return 0;
}

static void delay(unsigned long ms)
{
	struct timespec ts = { .tv_sec = (time_t)(ms / 1000),
			      .tv_nsec = (long)(ms % 1000) * 1000000L };

	while (!stopped && nanosleep(&ts, &ts) < 0 && errno == EINTR)
		;
}

int main(int argc, char **argv)
{
	const char *dir = "/sys/class/leds/alpha:green:status";
	unsigned long ms = 500, count = 10, i;
	struct sigaction sa = { .sa_handler = stop_handler };
	int blink, ret = EXIT_SUCCESS;

	if (argc < 2)
		goto usage;
	blink = strcmp(argv[1], "blink") == 0;
	if (blink) {
		if (argc < 4 || argc > 5 || number(argv[2], 3600000, &ms) ||
		    number(argv[3], 1000000, &count))
			goto usage;
		if (argc == 5)
			dir = argv[4];
	} else {
		if (argc > 3 || (strcmp(argv[1], "on") && strcmp(argv[1], "off")))
			goto usage;
		if (argc == 3)
			dir = argv[2];
	}

	/* Disable an existing kernel trigger before manual control. */
	if (write_attr(dir, "trigger", "none\n") < 0 && errno != ENOENT) {
		perror("disable LED trigger");
		return EXIT_FAILURE;
	}
	if (!blink) {
		if (write_attr(dir, "brightness", !strcmp(argv[1], "on") ? "1\n" : "0\n")) {
			perror("set LED brightness");
			return EXIT_FAILURE;
		}
		return EXIT_SUCCESS;
	}

	sigemptyset(&sa.sa_mask);
	if (sigaction(SIGINT, &sa, NULL) || sigaction(SIGTERM, &sa, NULL)) {
		perror("sigaction");
		return EXIT_FAILURE;
	}
	for (i = 0; i < count && !stopped; ++i) {
		if (write_attr(dir, "brightness", "1\n")) {
			perror("LED on");
			ret = EXIT_FAILURE;
			break;
		}
		delay(ms);
		if (write_attr(dir, "brightness", "0\n")) {
			perror("LED off");
			ret = EXIT_FAILURE;
			break;
		}
		delay(ms);
	}
	if (write_attr(dir, "brightness", "0\n")) {
		perror("final LED off");
		ret = EXIT_FAILURE;
	}
	return ret;

usage:
	fprintf(stderr, "Usage: %s on|off [LED_SYSFS_DIR]\n"
		"       %s blink INTERVAL_MS COUNT [LED_SYSFS_DIR]\n"
		"INTERVAL_MS: 1..3600000; COUNT: 1..1000000 full cycles\n",
		argv[0], argv[0]);
	return EXIT_FAILURE;
}
