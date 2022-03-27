obj-m += multi-flow-driver.o
multi-flow-driver-objs := multi-flow-dev.o dynamic-buffer.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules
	
debug:
	KCFLAGS="-DDEBUG" make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
