#define _DEFAULT_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdbool.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <err.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <linux/input.h>
#include <linux/uinput.h>

#include "uinput-key.h"

static void check_posix(intmax_t rc, const char *fmt, ...)
{
	if (rc < 0) {
		va_list args;
		va_start(args, fmt);
		verr(EXIT_FAILURE, fmt, args);
		va_end(args);
	}
}

static int ev_emit(int fd, int type, int code, int value)
{
	struct input_event ev = {
		.type = type,
		.code = code,
		.value = value
	};

	gettimeofday(&ev.time, 0);
	return write(fd, &ev, sizeof(ev));
}

static int ev_syn(int fd)
{
	return ev_emit(fd, EV_SYN, SYN_REPORT, 0);
}

static int ev_key(int fd, int key, int value)
{
	int ret;
	
	if (value && (SHIFT & key)) {
		if (ev_key(fd, KEY_LEFTSHIFT, 1) < 0)
			return -1;
	}
	
	ret = ev_emit(fd, EV_KEY, key & KEYMASK, value);
	if (ev_syn(fd) < 0)
		return -1;
	
	if (!value && (SHIFT & key)) {
		if (ev_key(fd, KEY_LEFTSHIFT, 0) < 0)
			return -1;
	}

	return ret;
}

static int ev_key_click(int fd, int key)
{
	if (ev_key(fd, key, 1) < 0)
		return -1;
	return ev_key(fd, key, 0);
}

static int ev_inject_keypress(int fd, const char c)
{
	const int key = printable_to_key(c);
	if (ev_key_click(fd, key) < 0)
		return -1;
	if (key == KEY_ENTER)
		usleep(500000);
	return 0;
}

static int ev_inject_keypresses(int fd, const char *msg)
{
	bool escaped = false;
	for (const char *c = msg; *c; ++c) {
		char key = *c;
		
		if (escaped) {
			escaped = false;
			
			if (key == 'n') {
				if (ev_inject_keypress(fd, '\n') < 0)
					return -1;
			} else if (key == 't') {
				if (ev_inject_keypress(fd, '\t') < 0)
					return -1;
			} else {
				if (ev_inject_keypress(fd, '\\') < 0)
					return -1;
				if (ev_inject_keypress(fd, key) < 0)
					return -1;
			}
		} else if (key == '\\') {
			escaped = true;
		} else if (ev_inject_keypress(fd, key) < 0) {
			return -1;
		}
	}
	
	return 0;
}

int main(int argc, char *argv[])
{
	char pid_filename[100];
	sprintf(pid_filename, "/var/run/%s.pid", basename(argv[0]));
	int pid_file = open(pid_filename, O_CREAT | O_RDWR, 0666);
	int rc = flock(pid_file, LOCK_EX | LOCK_NB);
	if(rc) {
		if(EWOULDBLOCK == errno) {
			// another instance is running
			//printf("already running");
			return -1;
		};
	}
	else {
		// this is the first instance
	};
	
	if (argc < 2)
		return -1;
	
	int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
	check_posix(fd, "failed to open uinput");
	
	check_posix(ioctl(fd, UI_SET_EVBIT, EV_KEY), "failed to set EV_KEY");
	check_posix(ioctl(fd, UI_SET_KEYBIT, KEY_LEFTSHIFT), "failed to set UI_SET_KEYBIT");
	
//	for (int key = KEY_1; key <= KEY_SPACE; ++key) {
	for (int key = KEY_ESC; key <= KEY_COMPOSE; ++key) {
		check_posix(ioctl(fd, UI_SET_KEYBIT, key & KEYMASK),
					"failed to set UI_SET_KEYBIT");
	}
	
	struct uinput_user_dev uidev = {
		.id.bustype = BUS_VIRTUAL,
		.id.vendor = 0x1,
		.id.product = 0x1,
		.id.version = 1
	};
	
	snprintf(uidev.name, UINPUT_MAX_NAME_SIZE, "Virtual Injector Keyboard");
	check_posix(write(fd, &uidev, sizeof(uidev)),
				"failed to write uinput_user_dev struct");
	check_posix(ioctl(fd, UI_DEV_CREATE),
				"failed to create uinput device");
	
	const int sleepms = 50;
	usleep(sleepms*1000);
	
	if (strcmp(argv[1],"--keycode")==0) {
		int key = keycode_to_key(atoi(argv[2]));
		if ((argv[3])&&((key == KEY_LEFTSHIFT)||(key == KEY_LEFTALT)||(key == KEY_LEFTCTRL))) {
			ev_key(fd, key, 1);
			key = keycode_to_key(atoi(argv[3]));
		};
		
		ev_emit(fd, EV_KEY, key, 1);
		ev_emit(fd, EV_KEY, key, 0);
		ev_syn(fd);
		usleep(sleepms*1000);
	}
	else {
		ev_inject_keypresses(fd, argv[1]);
	};
	
	ioctl(fd, UI_DEV_DESTROY);
	close(fd);
}
