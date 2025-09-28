include ../common.mk

MOUDLES = $(shell ls -d */)
OBJS    = $(shell ls ./*/*.o 2>/dev/null)
KERNEL_ELF = ../kernel-qemu.elf
KERNEL_BIN = ../kernel-qemu
KERNEL_LD  = kernel.ld

.PHONY: build clean $(MOUDLES) 

$(MOUDLES):
	$(MAKE) build --directory=$@


$(KERNEL_ELF): $(MODULES) $(KERNEL_LD)
	$(LD) $(LDFLAGS) -T $(KERNEL_LD) -o $(KERNEL_ELF) $(OBJS)

$(KERNEL_BIN): $(KERNEL_ELF)
	$(OBJCOPY) -O binary $(KERNEL_ELF) $(KERNEL_BIN)

build: $(MOUDLES) $(KERNEL_BIN)

clean:
	for d in $(MOUDLES); \
		do \
			$(MAKE) clean --directory=$$d; \
		done
	rm -f ${KERNEL_ELF} ${KERNEL_BIN}