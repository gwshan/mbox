// SPDX-License-Identifier: GPL-2.0+
/*
 * Userspace application to send an arbitrary ioctl command to /dev/test
 * so that the pre-define guest can be executed via Intel vmx.
 *
 * Author: Gavin Shan <shan.gavin@gmail.com> 2026
 */
#define _GNU_SOURCE
#include <sched.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>

int main(int argc, char **argv)
{
	int fd, ret;
	cpu_set_t mask;

	/* Avoid process migration as the driver can't handle it */
	CPU_ZERO(&mask);
	CPU_SET(1, &mask);
	ret = sched_setaffinity(0, sizeof mask, &mask);
	if (ret < 0) {
		fprintf(stderr, "%s: Error %d from sched_setaffinity()\n",
			__func__, ret);
		return ret;
	}

	/* Open /dev/test device */
	fd = open("/dev/test", O_RDWR);
	if (fd < 0) {
		fprintf(stdout, "%s: Error %d to open /dev/test\n",
			__func__, fd);
		return fd;
	}

	/* Arbitrary ioctl command triggers the execution */
	ioctl(fd, 0, NULL);

	close(fd);

	return 0;
}
