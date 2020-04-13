//general: ret[0] = 0 on success, error code on failure
//for printk: args[0] = offset to string, args[1] = length of string
//for physmem: args[0] = size of phys mem!

//not sure this is the right spot for these
#include "hypercall_opcodes.h"
#include <stdio.h>
#include <string.h>

//static void (*handle_hypercall[HYPCALL_COUNT])(struct hyp_shared* args);

//void handle_hypercall[HYP_physmem](struct hyp_shared* args) {
void handle_sizephysmem(struct hyp_shared* args) {
	//what do even do with it?
	u32_t size_phys_mem = args->args[0];
	printf("size phys mem = %d\n", size_phys_mem);
	args->ret[0] = 0;
}

//void handle_hypercall[HYP_printk](struct hyp_shared* args) {
void handle_print(struct hyp_shared* args) {
	//ok! let's print
	u32_t str_len = args->args[1];
	u32_t offset = args->args[0];
	//validate string() - check length constraints and for "/0"(?)
	char* str = (char*)((u32_t)(args) + offset);
	//struct stringargs *print = (struct stringargs*)(vm->mem);	
	//printf("Address of print: %x\n", print);
	//u32_t paddress = (u32_t)(print);
	//char* s2 = (char*)(paddress + print->offset);
	printf("string: %s\n", str);
	//alternatively: write(stdout, str, str_len);
	args->ret[0] = 0;
}
