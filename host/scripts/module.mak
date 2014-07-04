.PHONY: all clean install

$(MODULES): all

all:
	make -C $(KERNEL_DIR) M=$(CURDIR)

clean:
	make -C $(KERNEL_DIR) M=$(CURDIR) clean

install: $(MODULES)
	sudo make -C $(KERNEL_DIR) M=$(CURDIR) INSTALL_MOD_PATH=$(VM_DISK) modules_install
