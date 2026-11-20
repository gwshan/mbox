// SPDX-License-Identifier: GPL-2.0+
/*
 * This test program accesses physically contiguous memory chunk, exported
 * by '/dev/test-cache' or hugetlbfs. The contiguous memory exposed by the
 * '/dev/test-cache' is preferred. The accessed size is the L3 cache size.
 * So that any memory accesses in the tested workload will cause L3 cache
 * thrashing if L3 cache is equally shared between this test and the tested
 * workload. With proper L3 cache partitioning (isolation), the performance
 * of the tested workload should be improved.
 *
 * Author: Gavin Shan <gshan@redhat.com> 2026
 */
#define _GNU_SOURCE
#include <linux/perf_event.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sched.h>
#include <time.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <pthread.h>
#include <errno.h>

#include <linux/sched/types.h>
#include <linux/sched.h>

#ifndef MAP_HUGE_512MB
#define MAP_HUGE_512MB	(29U << 26)
#endif
#ifndef MAP_HUGE_1GB
#define MAP_HUGE_1GB	(30U << 26)
#endif

/* Fixed memory size supported by driver */
#define TEST_MEM_SIZE	0x10000000	/* 256MB */

#define cache_invalidate_and_clean(addr)	\
	__asm__ volatile("dc civac, %0" : : "r" (addr) : "memory")

struct test_struct {
	int		fd;		/* Device file descriptor                    */
	unsigned long	huge_page_size; /* Size of the huge page from hugetlbfs      */
	int		perf_fd;	/* Perf event file descriptor                */
	char		*group;		/* Resctrl group                             */
	int		*cpuset;	/* CPU set where the test will be executed   */
	int		scheduler;	/* Schduler: FIFO, RR, BATCH or IDLE         */
	int		force_exit;	/* Force threads to exit                     */
	pthread_t	*threads;	/* Handle of threads                         */
	unsigned long	*thread_exit;	/* Flag to indicate the thread exited        */
	int		ncpus;		/* Number of CPUs in the CPU set             */
	unsigned long	loops;		/* Number of loops the test will be executed */
	int		op_read;	/* Use read operation during the test        */
	int		trace;		/* Enable perf event tracing                 */

	unsigned long	llc_size;	/* LLC size in bytes                         */
	unsigned long	llc_line_size;	/* LLC line size in bytes                    */

	void		*buf;		/* Mapped virtual area                       */
};

static struct test_struct test;

static void usage(void)
{
	fprintf(stdout, "test [-g <group>] [-c <cpu>] [-s <scheduler>]\n");
	fprintf(stdout, "     [-l <loops>] [-o <read | write] [-t] [-h]\n");
	fprintf(stdout, "\n");
	fprintf(stdout, "-g    Target resctrl group\n");
	fprintf(stdout, "-c    Target CPU where the test is executed\n");
	fprintf(stdout, "-s    Scheduler which is normal, fifo, rr, batch, idle, \n");
	fprintf(stdout, "      deadline or ext\n");
	fprintf(stdout, "-l    Loops to execute the test\n");
	fprintf(stdout, "-o    Operation during the test, read or write\n");
	fprintf(stdout, "-t    Enable perf event tracing\n");
	fprintf(stdout, "-h    Help messages\n");
	fprintf(stdout, "\n");
}

static inline long perf_event_open(struct perf_event_attr *attr, pid_t pid,
				   int cpu, int group_fd, unsigned long flags)
{
	return syscall(SYS_perf_event_open, attr, pid, cpu, group_fd, flags);
}

static int init_test_struct(void)
{ 
	int ncpus, i, len;

	/* Initialize test struct */
	test.fd		        = -1;
	test.huge_page_size     = 0;
	test.perf_fd		= -1;
	test.ncpus		= 1;
	test.group		= NULL;
	test.scheduler		= -1;
	test.force_exit		= 0;
	test.loops		= 1;
	test.op_read		= 1;
	test.trace		= 0;
	test.llc_size		= 0;
	test.llc_line_size	= 0;
	test.buf		= (void *)-1;

	/*
	 * Initialize the CPU set. The last online CPU is the only
	 * one in the CPU set by default.
	 */
	ncpus = sysconf(_SC_NPROCESSORS_ONLN);
	test.cpuset = malloc(ncpus * sizeof(test.cpuset[0]));
	if (!test.cpuset) {
		fprintf(stderr, "Unable to alloc memory for CPU set\n");
		return -ENOMEM;
	}

	test.cpuset[0] = ncpus - 1;
	for (i = 1; i < ncpus; i++)
		test.cpuset[i] = -1;

	/* Initialize thread handlers */
	len = sizeof(test.threads[0]) * ncpus;
	test.threads = malloc(len);
	if (!test.threads) {
		fprintf(stderr, "Unable to alloc memory for thread handlers\n");
		return -ENOMEM;
	}

	memset(test.threads, 0, len);

	/* Initialize thread exiting flag */
	len = sizeof(test.thread_exit[0]) * 8;
	len = ((ncpus + len - 1) / len) * (len / 8);
	test.thread_exit = malloc(len);
	if (!test.thread_exit) {
		fprintf(stderr, "Unable to alloc memory for thread existing flag\n");
		return -ENOMEM;
	}

	memset(test.thread_exit, 0, len);

	return 0;
}

static int parse_scheduler(char *arg)
{
	if (!strcmp(arg, "normal"))
		test.scheduler = SCHED_NORMAL;
	else if (!strcmp(arg, "fifo"))
		test.scheduler = SCHED_FIFO;
	else if (!strcmp(arg, "rr"))
		test.scheduler = SCHED_RR;
	else if (!strcmp(arg, "batch"))
		test.scheduler = SCHED_BATCH;
	else if (!strcmp(arg, "idle"))
		test.scheduler = SCHED_IDLE;
	else if (!strcmp(arg, "deadline"))
		test.scheduler = SCHED_DEADLINE;
#ifdef SCHED_EXT
	else if (!strcmp(arg, "ext"))
		test.scheduler = SCHED_EXT;
#endif
	else
		return -EINVAL;

	return 0;
}

static int parse_cpuset(char *arg)
{
	char *str, *next, *tmp;
	int i, cpu, start, end;
	int ncpus = sysconf(_SC_NPROCESSORS_ONLN);

	/* Sanity check */
	if (!arg || strlen(arg) == 0)
		return 0;

	for (i = 0; i < strlen(arg); i++) {
		if (!(arg[i] == '-' || arg[i] == ',' || (arg[i] >= '0' && arg[i] <= '9'))) {
			fprintf(stderr, "Invalid CPU set parameter <%s>\n", arg);
			return -EINVAL;
		}
	}

	/* Copy over the argument */
	str = strdup(arg);
	if (!str) {
		fprintf(stderr, "Unable to duplicate CPU set parameter\n");
		return -ENOMEM;
	}

	/* A range of CPUs */
	next = strchr(str, '-');
	if (next) {
		*next++ = '\0';
		start = atoi(str);
		end = atoi(next);
		if (start > end || start < 0 || end >= ncpus) {
			fprintf(stderr, "Invalid CPU set [%d-%d]\n", start, end);
			return -ERANGE;
		}

		test.ncpus = (end - start) + 1;
		for (cpu = start; cpu <= end; cpu++)
			test.cpuset[cpu - start] = cpu;

		return 0;
	}

	/* A list of CPUs */
	i = 0;	
	tmp = str;
	while (next = strchr(tmp, ',')) {
		*next++ = '\0';
		cpu = atoi(tmp);
		if (cpu < 0 || cpu >= ncpus) {
			fprintf(stderr, "Invalid CPU %d\n", cpu);
			return -ERANGE;
		}

		test.cpuset[i++] = cpu;
		tmp = next;
	}

	cpu = atoi(tmp);
	if (cpu < 0 || cpu >= ncpus) {
		fprintf(stderr, "Invalid CPU %d\n", cpu);
		return -ERANGE;
	}

	test.cpuset[i++] = cpu;
	test.ncpus = i;

	return 0;
}

static int attach_resctrl_group(void)
{
	FILE *f;
	char path[1024], str[64];
	int ret;

	/* The root resctrl group will be used */
	if (!test.group)
		return 0;

	memset(path, 0, sizeof(path));
	snprintf(path, sizeof(path), "/sys/fs/resctrl/%s/tasks", test.group);
	f = fopen(path, "w");
	if (!f) {
		fprintf(stderr, "Resctrl group <%s> doesn't exist\n", test.group);
		return -EINVAL;
	}

	memset(str, 0, sizeof(str));
	snprintf(str, sizeof(str), "%d", getpid());
	ret = fputs(str, f);
	if (ret < 0) {
		fprintf(stderr, "Unable to write <%s>\n", path);
		fclose(f);
		return -EIO;
	}

	fclose(f);
	return 0;
}

static int set_scheduler(void)
{
	int min, max;
	struct sched_param param;
	struct sched_attr attr;

	/* Unspecified */
	if (test.scheduler < 0)
		return 0;

	memset(&param, 0, sizeof(param));
	memset(&attr, 0, sizeof(attr));

	switch (test.scheduler) {
	case SCHED_DEADLINE:
		min = sched_get_priority_min(test.scheduler);
		max = sched_get_priority_max(test.scheduler);
		attr.size = sizeof(attr);
		attr.sched_policy = SCHED_DEADLINE;
		attr.sched_flags = 0;
		attr.sched_nice = SCHED_OTHER;
		attr.sched_priority = min + (max - min) / 2;
		attr.sched_runtime = 90000;
		attr.sched_deadline = 100000;
		attr.sched_period = 0;
		return syscall(SYS_sched_setattr, getpid(), &attr, 0);
	case SCHED_FIFO:
	case SCHED_RR:
		min = sched_get_priority_min(test.scheduler);
		max = sched_get_priority_max(test.scheduler);
		param.sched_priority = min + (max - min) / 2;
		break;
	default:
		param.sched_priority = 0;
	}

	return sched_setscheduler(getpid(), test.scheduler, &param);
}

static int __get_cache_params(const char *filename, unsigned long *size)
{
	FILE *f;
	char path[1024], str[64], *p;
	unsigned long factor;

	memset(path, 0, sizeof(path));
	snprintf(path, sizeof(path),
		 "/sys/bus/cpu/devices/cpu0/cache/index3/%s", filename);
	f = fopen(path, "r");
	if (!f) {
		fprintf(stderr, "Unable to open <%s>\n", path);
		return -EIO;
	}

	if (fscanf(f, "%s", str) <= 0) {
		fprintf(stderr, "Unable to read <%s>\n", path);
		fclose(f);
		return -EIO;
	}

	factor = 1;
	p = &str[strlen(str) - 1];
	if (*p == 'K') {
		factor = 1024;
		*p = '\0';
	} else if (*p == 'M') {
		factor = 1024 * 1024;
		*p = '\0';
	}

	*size = atol(str) * factor;

	fclose(f);

	return 0;	
}

static int get_cache_params(void)
{
	int ret;

	ret = __get_cache_params("size", &test.llc_size);
	if (ret)
		return ret;

	ret = __get_cache_params("coherency_line_size", &test.llc_line_size);
	if (ret)
		return ret;

	if (test.llc_size >= 0x100000) {
		fprintf(stdout, "cache-size: %ldMB  cache-line-size: %ld\n",
			test.llc_size >> 20, test.llc_line_size);
	} else if (test.llc_size >= 0x400) {
		fprintf(stdout, "cache-size: %ldKB  cache-line-size: %ld\n",
			test.llc_size >> 10, test.llc_line_size);
	} else {
		fprintf(stdout, "cache-size: %ld    cache-line-size: %ld\n",
			test.llc_size, test.llc_line_size);
	}

	/*
	 * Ensure all available memory size is more than half of the
	 * L3 cache size.
	 */
	if (test.llc_size > TEST_MEM_SIZE) {
		fprintf(stderr, "Physically contiguous memory size 0x%x is too small\n",
			TEST_MEM_SIZE);
		return -EINVAL;
	}
 
	return 0;
}

static int setup_memory_test_cache(void)
{
	test.fd = open("/dev/test_cache", O_RDWR);
	if (test.fd < 0)
		return -ENOENT;

	test.buf = mmap(NULL, TEST_MEM_SIZE, PROT_READ | PROT_WRITE,
			MAP_SHARED, test.fd, 0);
	if (test.buf == (void *)-1) {
		fprintf(stderr, "Unable to map virtual area on cache file\n");
		close(test.fd);
		test.fd = -1;
		return -ENOMEM;
	}

	return 0;
}

static int reserve_memory_hugetlb(bool is_add)
{
	FILE *f;
	char path[256], str[64];
	unsigned long nr_huge_pages;

	/* Sanity check */
	if (test.huge_page_size == 0)
		return -EINVAL;

	/* Retrieve available number of huge pages */
	memset(path, 0, sizeof(path));
	snprintf(path, sizeof(path),
		 "/sys/kernel/mm/hugepages/hugepages-%ldkB/nr_hugepages",
		 test.huge_page_size / 1024);
	f = fopen(path, "r");
	if (!f) {
		fprintf(stderr, "Unable to open <%s> for reading\n", path);
		return -EIO;
	}

	if (fscanf(f, "%ld", &nr_huge_pages) != 1) {
		fprintf(stderr, "Unable to read <%s>\n", path);
		fclose(f);
		return -EIO;
	}

	fclose(f);

	/* We perhaps needn't do anything */
	if (!is_add && nr_huge_pages == 0)
		return 0;

	/* Set available number of huge pages */
	f = fopen(path, "w");
	if (!f) {
		fprintf(stderr, "Unable to open <%s> for writing\n", path);
		return -EIO;
	}

	memset(str, 0, sizeof(str));
	snprintf(str, sizeof(str), "%ld",
		 is_add ? nr_huge_pages + 1 : nr_huge_pages - 1);
	if (fputs(str, f) < 0) {
		fprintf(stderr, "Unable to write <%s>\n", path);
		fclose(f);
		return -EIO;
	}

	fclose(f);

	return 0;
}

static int setup_memory_hugetlb(void)
{
	unsigned long page_size;
	int flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB;
	int ret;

	/*
	 * Sort out the huge page size with assumption that we need
	 * 256MB memory, which is two times of the L3 capacity on
	 * NVidia's grace-hopper machine.
	 */
	page_size = sysconf(_SC_PAGESIZE);
	switch (page_size) {
	case 0x1000:  /* 4KB */
		test.huge_page_size = 0x40000000; /* 1GB */
		flags |= MAP_HUGE_1GB;
		break;
	case 0x4000:  /* 16KB */
		test.huge_page_size = 0x40000000; /* 1GB */
		flags |= MAP_HUGE_1GB;
		break;
	case 0x10000: /* 64KB */
		test.huge_page_size = 0x20000000; /* 512MB */
		flags |= MAP_HUGE_512MB;
		break;
	default:
		fprintf(stderr, "Unsupported page size (0x%x)\n", page_size);
		return -EINVAL;
	}

	/* Reserve huge page from hugetlbfs */
	ret = reserve_memory_hugetlb(true);
	if (ret) {
		fprintf(stderr, "Unable to reserve memory from hugetlbfs\n");
		test.huge_page_size = 0;
		return ret;
	}

	/* Map huge page from hugetlbfs */
	test.buf = mmap(NULL, TEST_MEM_SIZE, PROT_READ | PROT_WRITE, flags, -1, 0);
	if (test.buf == (void *)-1) {
		fprintf(stderr, "Unable to map hugetlbfs memory\n");
		return -ENOMEM;
	}

	return 0;	
}

static int setup_memory(void)
{
	int ret;

	ret = setup_memory_test_cache();
	if (!ret)
		return ret;

	ret = setup_memory_hugetlb();
	return ret;
}

static int perf_event_create(void)
{
	struct perf_event_attr attr;

	/* Nothing to be traced */
	if (!test.trace)
		return 0;

	memset(&attr, 0, sizeof(attr));
	attr.type = PERF_TYPE_HW_CACHE;
	attr.size = sizeof(attr);
	attr.disabled = 1;
	attr.exclude_kernel = 1;
	attr.exclude_hv = 1;
	if (test.op_read) {
		attr.config = PERF_COUNT_HW_CACHE_LL |
			      (PERF_COUNT_HW_CACHE_OP_READ << 8) |
			      (PERF_COUNT_HW_CACHE_RESULT_MISS << 16);
	} else {
		attr.config = PERF_COUNT_HW_CACHE_LL |
			      (PERF_COUNT_HW_CACHE_OP_READ << 8) |
			      (PERF_COUNT_HW_CACHE_RESULT_MISS << 16);
	}

	test.perf_fd = perf_event_open(&attr, 0, test.cpuset[0], -1,  0);
	if (test.perf_fd < 0) {
		fprintf(stderr, "Unable to open perf event\n");
		return -EIO;
	}

	return 0;
}

static inline void perf_event_start(void)
{
	if (test.perf_fd >= 0) {
		ioctl(test.perf_fd, PERF_EVENT_IOC_RESET, 0);
		ioctl(test.perf_fd, PERF_EVENT_IOC_ENABLE, 0);
	}
}

static inline void perf_event_stop(void)
{
	if (test.perf_fd >= 0)
		ioctl(test.perf_fd, PERF_EVENT_IOC_DISABLE, 0);
} 

static inline unsigned long perf_event_read(void)
{
	unsigned long count = 0;

	if (test.perf_fd >= 0)
		read(test.perf_fd, &count, sizeof(count));

	return count;
}

static void *test_thread(void *arg)
{
	int index = (int)(intptr_t)arg, i, ret;
	unsigned long addr, loop, elapsed, count;
	long offset;
	struct timespec tstart, tend;
	cpu_set_t mask;
	volatile unsigned char c;

        /* Set CPU affinity to the first CPU in the set */
	CPU_ZERO(&mask);
	CPU_SET(test.cpuset[index], &mask);
	ret = sched_setaffinity(0, sizeof(mask), &mask);
	if (ret) {
		fprintf(stdout, "Error %d to set affinity to CPU-%d\n",
			ret, test.cpuset[index]);
                goto out;
        }

	/* Create perf event */
	ret = perf_event_create();
	if (ret)
		goto out;

	/* Execute the test */
	clock_gettime(CLOCK_MONOTONIC, &tstart);
	perf_event_start();

	for (loop = 0; loop < test.loops; loop++) {
		if (test.force_exit)
			break;

		// __builtin___clear_cache(test.buf, test.buf + TEST_MEM_SIZE);
		// for (offset = 0; offset < test.llc_size; offset += test.llc_line_size) {
		for (offset = 0; offset < TEST_MEM_SIZE; offset += test.llc_line_size) {
			addr = (unsigned long)(test.buf) + offset;
			/* cache_invalidate_and_clean(addr); */
			if (test.op_read)
				c = *(volatile unsigned char *)addr;
			else
				*(volatile unsigned char *)addr = 0;
		}
	}

	perf_event_stop();
	count = perf_event_read();
	clock_gettime(CLOCK_MONOTONIC, &tend);
	elapsed = (tend.tv_nsec + 1000000000UL * tend.tv_sec) -
		  (tstart.tv_nsec + 1000000000UL * tstart.tv_sec);

	fprintf(stdout, "------------------ CPU-%d -------------------\n",
		test.cpuset[index]);
	if (test.trace) {
		fprintf(stdout, "%s %lld\n",
			test.op_read ? "Read misses:   " : "Write misses:  ",
			count);
	}
	fprintf(stdout, "Elapsed time:   %lld ms\n", elapsed / 1000000);
	fprintf(stdout, "\n");

out:
	/* Set thread exit flag */
	i = index % (sizeof(test.thread_exit[0]) * 8);
	test.thread_exit[index / (sizeof(test.thread_exit[0]) * 8)] |= (1UL << i);

	return NULL;
}

static void wait_on_all_threads(unsigned long interval)
{
	int i, j;
	bool to_exit;

	while (true) {
		if (interval)
			usleep(interval);

		to_exit = true;
		for (i = 0; i < test.ncpus; i++) {
			j = i % (sizeof(test.thread_exit[0]) * 8);
			if (!(test.thread_exit[i / (sizeof(test.thread_exit[0]) * 8)] &
			      (1UL << j))) {
				to_exit = false;
				break;
			}
		}

		if (to_exit)
			break;
	}

}

static void cleanup(void)
{
#if 0
	if (test.thread_exit) {
		free(test.thread_exit);
		test.thread_exit = NULL;
	}
#endif

	if (test.threads) {
		free(test.threads);
		test.threads = NULL;
	}

	if (test.cpuset) {
		free(test.cpuset);
		test.cpuset = NULL;
	}

	if (test.perf_fd >= 0) {
		close(test.perf_fd);
		test.perf_fd = -1;
	}

	if (test.buf != (void *)-1) {
		munmap(test.buf, TEST_MEM_SIZE);
		test.buf = (void *)-1;
	}

	if (test.huge_page_size) {
		reserve_memory_hugetlb(false);
		test.huge_page_size = 0;
	}

	if (test.fd >= 0) {
		close(test.fd);
		test.fd = -1;
	}
}

static void handle_sigint(int sig)
{
	test.force_exit = 1;
	wait_on_all_threads(0);
	cleanup();	
}

int main(int argc, char **argv)
{
	struct sigaction sa;
	cpu_set_t mask;
	int i, opt, ret = 0;

	/* Initialize test struct */
	ret = init_test_struct();
	if (ret)
		goto out;

	/* Parse input parameters */
	while ((opt = getopt(argc, argv, "g:c:s:l:o:th")) != -1) {
		switch (opt) {
		case 'g':
			test.group = optarg;
			break;
		case 'c':
			ret = parse_cpuset(optarg);
			if (ret)
				goto out;

			/* It's only tracable when we have only one CPU */
			if (test.ncpus > 1)
				test.trace = 0;
			break;
		case 's':
			ret = parse_scheduler(optarg);
			if (ret)
				goto out;

			break;
		case 'l':
			test.loops = atoll(optarg);
			break;
		case 'o':
			test.op_read = !strcmp(optarg, "read");
			break;
		case 't':
			if (test.ncpus > 1) {
				fprintf(stderr, "Not tracable for multiple CPUs\n");
				goto out;
			}

			test.trace = 1;
			break;
		case 'h':
		default:
			usage();
			goto out;
		}
	}

	/* Attach to the target resctrl group */
	ret = attach_resctrl_group();
	if (ret) {
		fprintf(stderr, "Error %d to attach resctrl group\n", ret);
		goto out;
	}

	/* Set CPU affinity to the first CPU in the set */
	CPU_ZERO(&mask);
	CPU_SET(test.cpuset[0], &mask);
	ret = sched_setaffinity(0, sizeof(mask), &mask);
	if (ret) {
		fprintf(stderr, "Error %d to set CPU affinity\n", ret);
		goto out;
	}

	/* Set scheduler */
	ret = set_scheduler();
	if (ret) {
		fprintf(stderr, "Error %d to set scheduler\n", ret);
		goto out;
	}

	/* Read LLC capacity */
	ret = get_cache_params();
	if (ret)
		goto out;

	/* Open device file */
	ret = setup_memory();
	if (ret)
		goto out;

	fprintf(stdout, "virtual area mapped at 0x%lx\n", (unsigned long)(test.buf));
	memset(test.buf, 0, TEST_MEM_SIZE);

	/* Register SIGINT handler */
	sa.sa_handler = &handle_sigint;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	ret = sigaction(SIGINT, &sa, NULL);
	if (ret) {
		fprintf(stderr, "Unable to register SIGINT handler\n");
		goto out;
	}

	/* Start threads */
	for (i = 0; i < test.ncpus; i++) {
		ret = pthread_create(&test.threads[i], NULL, test_thread,
				    (void *)(intptr_t)i);
		if (ret) {
			fprintf(stderr, "Unable to start thread %d\n", i);
			goto out;
		}
	}

	/* Wait all threads to exit */
	wait_on_all_threads(50000);

out:
	cleanup();

	return ret;
}
