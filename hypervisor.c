#include <fcntl.h>
#include <linux/kvm.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <signal.h>
#include <sys/time.h>

#include "debug.h"
#include "definition.h"
#include "hypercall.h"
#include "elf_loader.h"
//#include "hypercall_opcodes.h"
#include "hypercall_handlers.c"
//#define PS_LIMIT (0x1000000)
#define PS_LIMIT (0x10000000)
//#define PS_LIMIT (0x80000000)
#define KERNEL_STACK_SIZE (0x4000)
#define MAX_KERNEL_SIZE (PS_LIMIT - 0x5000 - KERNEL_STACK_SIZE)
#define MEM_SIZE (PS_LIMIT * 0x2)

#define MSR_PLATFORM_INFO 0x000000ce


static inline int
elf_load_info_single(struct elf_hdr *hdr, vaddr_t *ro_addr, size_t *ro_sz, char **ro_src)
{
	struct elf_contig_mem s = {};

	if(elf_contig_mem(hdr, 0, &s) || s.access != ELF_PH_RWX)
			return -1;

	*ro_addr = s.vstart;
	*ro_sz = s.sz;
	*ro_src = s.mem;

	//printf("vstart %p, sz %u, mem %p, entryaddr %p, mem %u\n", s.vstart, s.sz, s.mem, elf_entry_addr(hdr), s.mem);
	//printf("objsz %u sz %u\n", s.objsz, s.sz);

	return 0;

}

void read_file(const char *filename, uint8_t** content_ptr, size_t* size_ptr) {
	FILE *f = fopen(filename, "rb");
	if(f == NULL) error("Open file '%s' failed.\n", filename);
	if(fseek(f, 0, SEEK_END) < 0) pexit("fseek(SEEK_END)");

	size_t size = ftell(f);
	if(size == 0) error("Empty file '%s'.\n", filename);
	if(fseek(f, 0, SEEK_SET) < 0) pexit("fseek(SEEK_SET)");

	uint8_t *content = (uint8_t*) malloc(size);
	if(content == NULL) error("read_file: Cannot allocate memory\n");
	if(fread(content, 1, size, f) != size) error("read_file: Unexpected EOF\n");

	fclose(f);
	*content_ptr = content;
	*size_ptr = size;
}

void setup_protected_mode(VM *vm)
{
	struct kvm_sregs sregs;
	if(ioctl(vm->vcpufd, KVM_GET_SREGS, &sregs) < 0) pexit("ioctl(KVM_GET_SREGS)");
	struct kvm_segment seg = {
			.base = 0,
			.limit = 0xffffffff,
			.selector = 1 << 3,
			.present = 1,
			.type = 11, /* Code: execute, read, accessed                */
			.dpl = 0,
			.db = 1,
			.s = 1, /* Code/data */
			.l = 0,
			.g = 1, /* 4KB granularity */
	};

	sregs.cr0 |= CR0_PE; /* enter protected mallocode */

	sregs.cs = seg;

	seg.type = 3; /* Data: read/write, accessed */
	seg.selector = 2 << 3;
	sregs.ds = sregs.es = sregs.fs = sregs.gs = sregs.ss = seg;
	if(ioctl(vm->vcpufd, KVM_SET_SREGS, &sregs) < 0) pexit("ioctl(KVM_SET_SREGSs)");
}

/* set rip = entry point
 * set rsp = MAX_KERNEL_SIZE + KERNEL_STACK_SIZE (the max address can be used)
 *
 * set rdi = PS_LIMIT (start of free (unpaging) physical pages)
 * set rsi = MEM_SIZE - rdi (total length of free pages)
 * Kernel could use rdi and rsi to initalize its memory allocator.
 */
void setup_regs(VM *vm, int entry) {
	printf("\t\t\tentry: %zx\n", entry);
	struct kvm_regs regs;
	if(ioctl(vm->vcpufd, KVM_GET_REGS, &regs) < 0) pexit("ioctl(KVM_GET_REGS)");
	regs.rip = entry;
	regs.rsp = MAX_KERNEL_SIZE + KERNEL_STACK_SIZE; /* temporary stack */
	regs.rdi = PS_LIMIT; /* start of free pages */
	regs.rsi = MEM_SIZE - regs.rdi; /* total length of free pages */
	regs.rflags = 0x2;
	if(ioctl(vm->vcpufd, KVM_SET_REGS, &regs) < 0) pexit("ioctl(KVM_SET_REGS");
}

void setup_sregs(VM *vm) {
	struct kvm_sregs sregs;
	if(ioctl(vm->vcpufd, KVM_GET_SREGS, &sregs) < 0) pexit("ioctl(KVM_GET_REGS)");
	sregs.cs.selector = 0;
	sregs.cs.base = 0;
	if(ioctl(vm->vcpufd, KVM_SET_SREGS, &sregs) < 0) pexit("ioctl(KVM_SET_REGS");
}

void setup_msr(VM *vm) {
	u64_t msr_data = 24 << 8; //0x80060011900 <-- from my system

	struct kvm_msr_entry msr_entry;
	msr_entry.index = MSR_PLATFORM_INFO;
	msr_entry.data = msr_data;
	
	struct kvm_msrs * msrs = (struct kvm_msrs *)malloc(sizeof(struct kvm_msr_entry) + sizeof(struct kvm_msrs));
	msrs->nmsrs = 1;
	*msrs->entries = msr_entry;

	if(ioctl(vm->vcpufd, KVM_SET_MSRS, msrs) == -1) {
		pexit("ioctl(KVM_SET_MSRS) failed");
	}
}

//extern const unsigned char code[], end[];
//extern const unsigned char guest16[], guest16_end[];

// Phys = 0x00100000 = 1048576
// Entry point = 0x0010000c = 1048588
// Phys of sec 2 = 0x00102000 = 1056768
VM* kvm_init(uint8_t code[], size_t len) { 

	struct elf_contig_mem s[3] = {};
	struct elf_hdr *hdr = (struct elf_hdr *)code;
	int retval;

	if(elf_contig_mem(hdr, 0, &s[0]) || s[0].access != ELF_PH_CODE)
			pexit("first FAILED ELF_CONFIG");
	// BUG
	if(elf_contig_mem(hdr, 1, &s[1]) || s[0].access != ELF_PH_CODE)
			pexit("second FAILED ELF_CONFIG");

	if((retval = elf_contig_mem(hdr, 2, &s[2])) != 1) {
			printf("retval from elf_contig_mem = %d\n", retval);
			pexit("third FAILED ELF_CONFIG");
	}

//  DEBUG
//  printf("seg 1 vstart %p, sz %u, s.mem %p, entryaddr %p, s.mem %u\n", s[0].vstart, s[0].sz, s[0].mem, elf_entry_addr(hdr), s[0].mem);
//  printf("objsz %u sz %u\n", s[0].objsz, s[0].sz);
//  printf("seg 2 vstart %p, sz %u, s.mem %p, entryaddr %p, s.mem %u\n", s[1].vstart, s[1].sz, s[1].mem, elf_entry_addr(hdr), s[1].mem);
//  printf("objsz %u sz %u\n", s[1].objsz, s[1].sz);

	int kvmfd = open("/dev/kvm", O_RDWR | O_CLOEXEC);
	if(kvmfd < 0) pexit("open(/dev/kvm)");

	int api_ver = ioctl(kvmfd, KVM_GET_API_VERSION, 0);
		if(api_ver < 0) pexit("KVM_GET_API_VERSION");
	if(api_ver != KVM_API_VERSION) {
		error("Got KVM api version %d, expected %d\n",
			api_ver, KVM_API_VERSION);
	}
	int vmfd = ioctl(kvmfd, KVM_CREATE_VM, 0);
	if(vmfd < 0) pexit("ioctl(KVM_CREATE_VM)");
	char *mem = mmap(0,
		MEM_SIZE,
		PROT_READ | PROT_WRITE | PROT_EXEC,
		MAP_SHARED | MAP_ANONYMOUS,
		-1, 0);
	if(mem == NULL) pexit("mmap(MEM_SIZE)");
	int entry = 0x0010000c;//elf_entry_addr(hdr);
//  int entry = 0x00100000;//elf_entry_addr(hdr);

	printf("roundup %lu\n", round_up_to_page(s[0].objsz));
//  memcpy(mem + 0, s[0].mem, s[0].objsz);
	memcpy(mem + 0x00100000, s[0].mem, s[0].objsz);
//  printf("offsetasd %u, roundup %u\n", offset, round_up_to_page(s.objsz));
//  memset(mem + 0 + s[0].objsz, 0, s[0].sz - s[0].objsz);
	memset(mem + 0x00100000 + s[0].objsz, 0, s[0].sz - s[0].objsz);

	printf("TESTSSSS\n");

//  memcpy(mem + 0x00002000, s[1].mem, s[1].objsz);
	memcpy(mem + 0x0011e000, s[1].mem, s[1].objsz);

	printf("test...\n" ); 
	printf("vstart1 %ld, sz %ld, mem %p, entryaddr %ld, mem %p\n", s[0].vstart, s[0].sz, s[0].mem, elf_entry_addr(hdr), s[0].mem);
	printf("objsz1 %ld sz %ldp\n", s[0].objsz, s[0].sz);
	printf("vstart2 %ld, sz %ld, mem %p, entryaddr %ld, mem %p\n", s[1].vstart, s[1].sz, s[1].mem, elf_entry_addr(hdr), s[1].mem);
	printf("objsz2 %ld sz %ld\n", s[1].objsz, s[1].sz);
	
	//  memset(mem + 0x00002000 + s[1].objsz, 0, s[1].sz - s[1].objsz);
	memset(mem + 0x0011e000 + s[1].objsz, 0, s[1].sz - s[1].objsz);

	printf("TESTSSSS2\n");

	u8_t * add = mem + 0x10000c;
	u8_t * add2 = s[0].mem + 0x0c;
	printf("\t%x\t%x\t%s\n", *add, *add2, mem);
	printf("\t%x\t%x\t%s\n", *(add+1), *(add2+1), mem);
	//  typedef void (*fn_t)(void);
	//  ((fn_t)add)();

	//objszWTF
	//  memcpy(mem, s.mem + round_to_page(s.objsz), offset);
	//  memset(mem + offset, 0, s.sz);

	//  memcpy((void*) mem + entry, code, len);
	//  memcpy((void*) mem + entry, code, end-code);
	// memcpy((void*) mem + entry, guest16, guest16_end-guest16);
	//  memcpy((void*) mem + entry, loader, loader_end-loader);

	struct kvm_userspace_memory_region region = {
		.slot = 0,
		.flags = 0,
		.guest_phys_addr = 0,
		.memory_size = MEM_SIZE,
		.userspace_addr = (size_t) mem
	};
	if(ioctl(vmfd, KVM_SET_USER_MEMORY_REGION, &region) < 0) {
		pexit("ioctl(KVM_SET_USER_MEMORY_REGION)");
	}
	int vcpufd = ioctl(vmfd, KVM_CREATE_VCPU, 0);
	if(vcpufd < 0) pexit("ioctl(KVM_CREATE_VCPU)");
	size_t vcpu_mmap_size = ioctl(kvmfd, KVM_GET_VCPU_MMAP_SIZE, NULL);
	struct kvm_run *run = (struct kvm_run*) mmap(0,
		vcpu_mmap_size,
		PROT_READ | PROT_WRITE,
		MAP_SHARED,
		vcpufd, 0);

	VM *vm = (VM*) malloc(sizeof(VM));
	*vm = (struct VM){
		.mem = mem,
		.mem_size = MEM_SIZE,
		.vcpufd = vcpufd,
		.run = run
	};

	setup_regs(vm, entry);
	setup_sregs(vm);
	setup_protected_mode(vm);

	setup_msr(vm);


	return vm;
}

int check_iopl(VM *vm) {
	struct kvm_regs regs;
	struct kvm_sregs sregs;
	if(ioctl(vm->vcpufd, KVM_GET_REGS, &regs) < 0) pexit("ioctl(KVM_GET_REGS)");
	if(ioctl(vm->vcpufd, KVM_GET_SREGS, &sregs) < 0) pexit("ioctl(KVM_GET_SREGS)");
	return sregs.cs.dpl <= ((regs.rflags >> 12) & 3);
}

void execute(VM* vm) {
	hypercall_handlers_init();
	while(1) {  
	ioctl(vm->vcpufd, KVM_RUN, NULL);
			dump_regs(vm->vcpufd);

	if(vm->run->ready_for_interrupt_injection) {
		//Inject interrupt #13: General Protection Fault
		// struct kvm_interrupt interrupt = { .irq = 13};

		// if(ioctl(vm->vcpufd, KVM_INTERRUPT, &interrupt) != 0) {
		// 	perror("Error injecting interrupt\n");
		// }
	}
		
	switch (vm->run->exit_reason) {
		case KVM_EXIT_HLT:
			fprintf(stderr, "KVM_EXIT_HLT\n");
			return;
		case KVM_EXIT_IO:
			//check the opcode and interpret the shared struct args accordingly
			if(!check_iopl(vm)) error("KVM_EXIT_SHUTDOWN\n");
			if(vm->run->io.port == 0xE9){

					struct hyp_shared *args = (struct hyp_shared*)(vm->mem);
					if(args->opcode >= 0 && args->opcode < HYPCALL_COUNT) {
							hypercall_handlers[args->opcode](args);
					}
					continue;
			}
			else if(vm->run->io.port == 0xE8) {
				char *p = (char *)vm->run;
				fwrite(p + vm->run->io.data_offset, vm->run->io.size, 1, stdout);
				fflush(stdout);	
				continue;
			}

			if(vm->run->io.port & HP_NR_MARK) {
				if(hp_handler(vm->run->io.port, vm) < 0) error("Hypercall failed\n");
			}
			else error("Unhandled I/O port: 0x%x\n", vm->run->io.port);
			break;
			
		case KVM_EXIT_FAIL_ENTRY:
				error("KVM_EXIT_FAIL_ENTRY: hardware_entry_failure_reason = 0x%llx\n",
					vm->run->fail_entry.hardware_entry_failure_reason);
		case KVM_EXIT_INTERNAL_ERROR:
				error("KVM_EXIT_INTERNAL_ERROR: suberror = 0x%x\n",
				vm->run->internal.suberror);
		case KVM_EXIT_SHUTDOWN:
				error("KVM_EXIT_SHUTDOWNs\n");
		default:
				error("Unhandled reason: %d\n", vm->run->exit_reason);
		}

	}
}
int main(int argc, char *argv[]) {
		uint8_t *code = 0;
		size_t len = 0 ;
		read_file(argv[1], &code, &len);
		if(len > MAX_KERNEL_SIZE)
			error("Kernel size exceeded, %p > MAX_KERNEL_SIZE(%p).\n",
				(void*) len,
				(void*) MAX_KERNEL_SIZE);

		VM* vm = kvm_init(code, len);
		printf("test 1\n");
		execute(vm);
}
