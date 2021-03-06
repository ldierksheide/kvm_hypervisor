#include Makefile.src

CC := gcc -m32
LD := ld -m elf_i386
AS := as --32 -g

INCPATH := ../../kernel/include
INCS    := -I$(INCPATH)
CFLAGS  := -g3 -O3 -ffreestanding -Wno-unused-function -Wno-unused-variable -Wno-unused-but-set-variable -nostdinc -nostdlib -fno-pic -mno-red-zone $(INCS)
LDFLAGS := -nostdlib -fno-builtin -nostartfiles -nostdinc -nodefaultlibs

KERNEL := kernel.img

WARNINGS += -Wall
WARNINGS += -Wextra
WARNINGS += -Wcast-align
WARNINGS += -Wformat=2
WARNINGS += -Winit-self
#WARNINGS += -Wmissing-declarations
#WARNINGS += -Wmissing-prototypes
WARNINGS += -Wnested-externs
WARNINGS += -Wno-system-headers
WARNINGS += -Wold-style-definition
WARNINGS += -Wredundant-decls
WARNINGS += -Wsign-compare
WARNINGS += -Wstrict-prototypes
WARNINGS += -Wundef
WARNINGS += -Wvolatile-register-var
WARNINGS += -Wwrite-strings

CFLAGS += $(WARNINGS)

#OBJS += kernel.o
#OBJS += gdt.o
#OBJS += idt.o
#OBJS += vm.o
#OBJS += printk.o
#OBJS += string.o
#OBJS += vtxprintf.o
#OBJS += tss.o
#OBJS += user.o
#OBJS += serial.o
#OBJS += hpet.o
#OBJS += chal.o
#OBJS += boot_comp.o
#OBJS += miniacpi.o
##OBJS += console.o
#OBJS += vga.o
#OBJS += exception.o
#OBJS += lapic.o
#
#COS_OBJ += pgtbl.o
#COS_OBJ += retype_tbl.o
#COS_OBJ += liveness_tbl.o
#COS_OBJ += tcap.o
#COS_OBJ += capinv.o
#COS_OBJ += captbl.o
OBJS += guest.o

DEPS :=$(patsubst %.o, %.d, $(OBJS))

OBJS += $(COS_OBJ)

TARGET := hypervisor.elf
SRCS := hypervisor.c hypercall.c
DEPS2 := definition.h debug.h
#DEPS2 := definition.h debug.h loader.o guest.o
#DEPS2 := definition.h debug.h payload.o loader.o
CFLAGS := -Wall

ifdef KVM_DEBUG
CFLAGS += -DDEBUG
endif

all: $(TARGET)

$(TARGET): $(SRCS) $(DEPS2)
	$(info |     [CC]   Compiling $@)
	$(CC) $^ $(CFLAGS) -o $@

#payload.o: payload.ld guest16.o
#	$(info |     [LD]   Linking $@)
#	$(LD) -T $< -o $@

debug:
	$(MAKE) KVM_DEBUG=1

all: $(KERNEL)

$(KERNEL): linker.ld $(DEPS) $(OBJS) loader.o
	$(info |     [LD]   Linking $@)
	$(LD) -T linker.ld loader.o $(OBJS) -o $@

loader.o: loader.S entry.S
	$(info |     [AS]   Assembling $@)
	$(CC) -c -I$(INCPATH) entry.S
	$(CC) -c -I$(INCPATH) loader.S

%.d: %.c
	$(CC) -M -MT $(patsubst %.d, %.o, $@) $(CFLAGS) $< -o $@
#$(LDFLAGS)
#pgtbl.o: ../../kernel/pgtbl.c
#	$(info |     [CC]   Compiling $@)
#	@$(CC) $(CFLAGS) -c $< -o $@
#
#tcap.o: ../../kernel/tcap.c
#	$(info |     [CC]   Compiling $@)
#	@$(CC) $(CFLAGS) -c $< -o $@
#
#retype_tbl.o: ../../kernel/retype_tbl.c
#	$(info |     [CC]   Compiling $@)
#	@$(CC) $(CFLAGS) -c $< -o $@
#
#liveness_tbl.o: ../../kernel/liveness_tbl.c
#	$(info |     [CC]   Compiling $@)
#	@$(CC) $(CFLAGS) -c $< -o $@
#
#capinv.o: ../../kernel/capinv.c
#	$(info |     [CC]   Compiling $@)
#	@$(CC) $(CFLAGS) -c $< -o $@
#
#captbl.o: ../../kernel/captbl.c
#	$(info |     [CC]   Compiling $@)
#	@$(CC) $(CFLAGS) -c $< -o $@
#
#
%.o: %.c
	$(info |     [CC]   Compiling $@)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f *.d *.o $(KERNEL) $(TARGET)

cp: $(KERNEL)
	$(info |     [CP]   Copying native booter to $(TRANS_DIR))
	cp -f $(KERNEL) .gdbinit *.sh $(TRANS_DIR)
	cp runscripts/*.sh $(TRANS_DIR)
