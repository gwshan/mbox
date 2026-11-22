PWD  := $(shell pwd)
KDIR ?= /lib/modules/$(shell uname -r)/build

obj-m += test.o
test-objs := main.o vm.o vcpu.o handler.o

default:
	$(MAKE) -C $(KDIR) M=$(PWD) modules
clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
guest:
	gcc -g -nostdinc -c guest.S -o guest.o
	ld -Ttext=0x0 -nostdlib -static guest.o -o guest.elf
	objcopy -O binary guest.elf guest.bin
	xxd -i guest.bin > guest.h
app:
	gcc app.c -o app
