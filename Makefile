modname := hid-hp-zbook
obj-m := $(modname).o
ccflags-y := -std=gnu99

KVERSION := $(shell uname -r)
KDIR := /lib/modules/$(KVERSION)/build
PWD := "$$(pwd)"
MODULE_VERSION := 0.1.3

ifdef DEBUG
CFLAGS_$(obj-m) := -DDEBUG
endif

default:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) O=$(PWD) -C $(KDIR) M=$(PWD) clean

load:
	-rmmod $(modname)
	insmod $(modname).ko

install:
	mkdir -p /lib/modules/$(KVERSION)/misc/$(modname)
	install -m 0755 -o root -g root $(modname).ko /lib/modules/$(KVERSION)/misc/$(modname)
	depmod -a

uninstall:
	rm /lib/modules/$(KVERSION)/misc/$(modname)/$(modname).ko
	rmdir /lib/modules/$(KVERSION)/misc/$(modname)
	rmdir /lib/modules/$(KVERSION)/misc
	depmod -a