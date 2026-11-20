// SPDX-License-Identifier: GPL-2.0+
/*
 * This test program runs on the host, to receive GPA outputed by 'victimd'
 * from the guest. The GPA is translated to HPA, and recoverable error
 * is inject to HPA automatically.
 *
 * NOTE: We have the assumption that the guest has only one NUMA node and
 * the memory capacity is 4GB. The test program won't work if the assumption
 * is broken.
 *
 * Author: Gavin Shan <gshan@redhat.com>
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <errno.h>
#include <time.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/wait.h>

#define TEST_GUEST_MEM_SIZE	0x100000000	/* 4GB */
#define TEST_GUEST_MEM_NODES	2		/* Number of NUMA nodes */
#define TEST_GUEST_MEM_START	0x040000000	/* 1GB */
#define TEST_INJECT_ERROR_TYPE	0x10

struct test_struct {
	int pid;
	unsigned long	guest_mem_size;
	unsigned long	guest_mem_nodes;
	unsigned long	gpa;
	unsigned long	hva;
	unsigned long	hpa;
};

static void usage(void)
{
	fprintf(stdout, "\n");
	fprintf(stdout, "./test <gpa>\n");
	fprintf(stdout, "gpa  The GPA (Guest Physical Address) where the error is injected\n");
	fprintf(stdout, "\n");
}

static void init_test_struct(struct test_struct *test)
{
	test->pid		= -1;
	test->guest_mem_size	= TEST_GUEST_MEM_SIZE;
	test->guest_mem_nodes	= TEST_GUEST_MEM_NODES;
	test->gpa		= -1UL;
	test->hpa		= -1UL;
}

static int fetch_gpa(struct test_struct *test, int argc, char **argv)
{
	if (argc != 2) {
		usage();
		return -EINVAL;
	}

	test->gpa = strtoul(argv[1], NULL, 16);
	if (test->gpa < TEST_GUEST_MEM_START ||
	    test->gpa >= (TEST_GUEST_MEM_START + test->guest_mem_size)) {
		fprintf(stderr, "%s: GPA 0x%lx out of range [1GB, 1GB+0x%lx]\n",
			__func__, test->gpa, test->guest_mem_size);
		return -EINVAL;
	}

	return 0;
}

static int find_qemu_pid(struct test_struct *test)
{
	DIR *dir;
	FILE *fp;
	struct dirent *entry;
	char path[256], data[256];
	size_t sz;
	int ret = -ENODEV;

	dir = opendir("/proc");
	if (!dir) {
		fprintf(stderr, "%s: unable to open </proc>\n", __func__);
		return -EIO;
	}

	while ((entry = readdir(dir)) != NULL) {
		if (entry->d_type != DT_DIR || entry->d_name[0] == '.')
			continue;

		memset(path, 0, sizeof(path));
		snprintf(path, sizeof(path), "/proc/%s/comm", entry->d_name);
		fp = fopen(path, "r");
		if (!fp)
			continue;

		memset(data, 0, sizeof(data));
		sz = fread(data, 1, sizeof(data), fp);
		fclose(fp);
		if (sz <= 0)
			continue;

		if (strstr(data, "qemu")) {
			ret = 0;
			test->pid = atoi(entry->d_name);
			break;
		}
	}

	if (ret != 0)
		fprintf(stderr, "%s: Unable to find QEMU PID\n", __func__);

	closedir(dir);
	return ret;
}

static int fetch_hva(struct test_struct *test)
{
	FILE *fp;
	char filename[64], *data = NULL, *next, *next1;
	unsigned long start, end, area_size;
	size_t sz, len;
	int area_index, ret = -EIO;

	memset(filename, 0, sizeof(filename));
	snprintf(filename, sizeof(filename), "/proc/%d/smaps", test->pid);
	fp = fopen(filename, "r");
	if (!fp) {
		fprintf(stderr, "%s: Unable to open <%s>\n", __func__, filename);
		return ret;
	}

	area_size = test->guest_mem_size / test->guest_mem_nodes;
        area_index = (test->gpa - TEST_GUEST_MEM_START) / area_size; 

	while ((sz = getline(&data, &len, fp)) != -1) {
		if (!strstr(data, "rw-p"))
			continue;

		next = strchr(data, '-');
		if (!next)
			continue;

		*next++ = '\0';
		next1 = strchr(next, ' ');
		if (!next1)
			continue;

		*next1 = '\0';
		start = strtoul(data, NULL, 16);
		end = strtoul(next, NULL, 16);
		if ((end - start) != area_size)
			continue;

		if (--area_index < 0) {
			ret = 0;
			test->hva = start + (test->gpa - TEST_GUEST_MEM_START) % area_size;
			break;
		}
	}

	if (data)
		free(data);

	fclose(fp);
	return ret;
}

static int fetch_hpa(struct test_struct *test)
{
	int fd;
	unsigned long pinfo, pgsize = getpagesize();
	off_t offset = (test->hva / pgsize) * sizeof(pinfo);
	char filename[128];
	ssize_t sz;

	memset(filename, 0, sizeof(filename));
	snprintf(filename, sizeof(filename), "/proc/%d/pagemap", test->pid);
	fd = open(filename, O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "%s: Unable to open <%s>\n", __func__, filename);
		return -EIO;
	}

	sz = pread(fd, &pinfo, sizeof(pinfo), offset);
	close(fd);
	if (sz != sizeof(pinfo)) {
		fprintf(stderr, "%s: Unable to read from <%s>\n", __func__, filename);
		return -EIO;
	}

	if (!(pinfo & (1UL << 63))) {
		fprintf(stderr, "%s: Page not present\n", __func__);
		return -EINVAL;
	}

	test->hpa = ((pinfo & 0x007fffffffffffffUL) * pgsize) + (test->hva & (pgsize - 1));
	return 0;
}

static int write_file(const char *filename, unsigned long val)
{
	int fd;
	char data[128];
	size_t sz;
	int ret = 0;

	memset(data, 0, sizeof(data));
	sz = snprintf(data, sizeof(data), "0x%lx", val);

	fd = open(filename, O_WRONLY);
	if (fd < 0) {
		fprintf(stderr, "%s: Unable to open <%s>\n", __func__, filename);
		return -EIO;
	}

	if (write(fd, data, sz) != sz) {
		ret = -EIO;
		fprintf(stderr, "%s: Unable to write <%s>\n", __func__, filename);
	}

	close(fd);
	return ret;
}

static int inject_error(struct test_struct *test)
{
	fprintf(stdout, "pid:	%d\n", test->pid);
	fprintf(stdout, "gpa:	0x%lx\n", test->gpa);
	fprintf(stdout, "hva:	0x%lx\n", test->hva);
	fprintf(stdout, "hpa:	0x%lx\n", test->hpa);

	system("modprobe einj > /dev/null");
	if (write_file("/sys/kernel/debug/apei/einj/param1",		test->hpa) 		||
	    write_file("/sys/kernel/debug/apei/einj/param2",		0xfffffffffffff000)	||
	    write_file("/sys/kernel/debug/apei/einj/flags",		0x0)			||
	    write_file("/sys/kernel/debug/apei/einj/error_type",	TEST_INJECT_ERROR_TYPE)	||
	    write_file("/sys/kernel/debug/apei/einj/notrigger",		1)			||
	    write_file("/sys/kernel/debug/apei/einj/error_inject",	1))
		return -EIO;

	return 0;
}

int main(int argc, char **argv)
{
	struct test_struct test;
	int ret;

	init_test_struct(&test);

	if (fetch_gpa(&test, argc, argv) ||
	    find_qemu_pid(&test)	 ||
	    fetch_hva(&test)		 ||
	    fetch_hpa(&test)		 ||
	    inject_error(&test))
		return -EIO;

	return 0;
}
