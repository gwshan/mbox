arch := arm64

default:
	gcc -DARCH=$(arch) -Iinc lib/xarray.c test/lib/xarray.c main.c -o mbox
