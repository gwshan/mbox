/* SPDX-License-Identifier: GPL-2.0+ */

#include <mbox/mbox.h>
#include <mbox/test.h>

int main(int argc, char **argv)
{
	atomic_t a;

	a.counter = 1;
	arch_atomic_add(&a, 1);
	fprintf(stdout, "%d\n", a.counter);

	//test_lib_xarray();

	return 0;
}

