//general: ret[0] = 0 on success, error code on failure
//for printk: args[0] = offset to string, args[1] = length of string
//for physmem: args[0] = size of phys mem!
//for writemsr: args[0] = msr

#include "hypercall_opcodes.h"
#include <stdio.h>
#include <string.h>
#include <linux/kvm.h>

#define MAX_HANDLERS 5
#define SIZE_PHYS_MEM 0x3d09000

void handle_sizephysmem(struct hyp_shared* args); 
void handle_print(struct hyp_shared* args);

static void (*hypercall_handlers[MAX_HANDLERS])(struct hyp_shared*);
static unsigned num_handlers = 0;

int
hypercall_register_handler(void (*handler)(struct hyp_shared*), int opcode)
{
	if (handler == NULL || num_handlers > (sizeof(hypercall_handlers) / sizeof(hypercall_handlers[0]))) return -1;
	hypercall_handlers[opcode] = handler;
	num_handlers++;
	return 0;
}

void hypercall_handlers_init() {
	//register our functions hypercall handlers
	void (*func_ptr)(struct hyp_shared*) = &handle_sizephysmem;
	hypercall_register_handler(func_ptr, HYP_physmem);

	func_ptr = &handle_print;
	hypercall_register_handler(func_ptr, HYP_printk);
}

void handle_sizephysmem(struct hyp_shared* args) {
	args->ret[0] = 0;
	args->ret[1] = SIZE_PHYS_MEM;
}

void handle_print(struct hyp_shared* args) {
	u32_t str_len = args->args[1];
	u32_t offset = args->args[0];

	/*TODO: validate string */
	char* str = (char*)((u32_t)(args) + offset);
	
	printf("%s", str);
	args->ret[0] = 0;
}