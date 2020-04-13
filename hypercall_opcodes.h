#define HYP_printk 0
#define HYP_physmem 1

#define HYPCALL_COUNT 2
struct hyp_shared {
	u32_t opcode;
	u32_t args[5];
	u32_t ret[5];
};
//general: ret[0] = 0 on success, error code on failure
//for printk: args[0] = offset to string, args[1] = length of string
//for physmem: args[0] = size of phys mem!
