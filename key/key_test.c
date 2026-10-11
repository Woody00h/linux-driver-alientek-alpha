// SPDX-License-Identifier: GPL-2.0
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

static volatile sig_atomic_t stop;

static void on_signal(int sig)
{
	(void)sig;
	stop = 1;
}

int main(int argc, char **argv)
{
	struct sigaction sa = { .sa_handler = on_signal };
	struct input_event events[16];
	char name[256] = { 0 };
	int fd, result = 0;

	if (argc != 2) {
		fprintf(stderr, "Usage: %s /dev/input/eventX\n", argv[0]);
		return 1;
	}
	sigemptyset(&sa.sa_mask);
	if (sigaction(SIGINT, &sa, NULL) || sigaction(SIGTERM, &sa, NULL)) {
		perror("sigaction");
		return 1;
	}
	fd = open(argv[1], O_RDONLY | O_CLOEXEC);
	if (fd < 0) {
		perror(argv[1]);
		return 1;
	}
	if (ioctl(fd, EVIOCGNAME(sizeof(name)), name) < 0) {
		perror("EVIOCGNAME");
		close(fd);
		return 1;
	}
	if (strcmp(name, "alpha-key")) {
		fprintf(stderr, "Expected alpha-key, got %s\n", name);
		close(fd);
		return 1;
	}
	printf("Listening to %s (%s), Ctrl+C to exit\n", argv[1], name);
	while (!stop) {
		ssize_t n = read(fd, events, sizeof(events));
		size_t i;

		if (n < 0) {
			if (errno == EINTR)
				continue;
			perror("read");
			result = 1;
			break;
		}
		if (!n || n % sizeof(events[0])) {
			fprintf(stderr, "Unexpected input event stream end/size\n");
			result = 1;
			break;
		}
		for (i = 0; i < (size_t)n / sizeof(events[0]); i++) {
			struct input_event *e = &events[i];

			if (e->type == EV_SYN && e->code == SYN_DROPPED) {
				/* Stop rather than print unreliable press/release pairs. */
				fprintf(stderr, "Input events lost; restart test\n");
				result = 1;
				stop = 1;
				break;
			}
			if (e->type == EV_KEY && e->code == KEY_ENTER) {
				printf("%ld.%06ld %s\n", (long)e->input_event_sec,
				       (long)e->input_event_usec,
				       e->value == 1 ? "PRESSED" :
				       e->value == 0 ? "RELEASED" : "REPEAT");
				fflush(stdout);
			}
		}
	}
	close(fd);
	return result;
}
