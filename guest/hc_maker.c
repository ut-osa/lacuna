#include <stdio.h>

#define KVM_HC_VM_NUM                   258

#define KVM_HYPERCALL ".byte 0x0f,0x01,0xc1"
static inline long kvm_hypercall0(unsigned int nr)
{
	long ret;
	asm volatile(KVM_HYPERCALL
		     : "=a"(ret)
		     : "a"(nr)
		     : "memory");
	return ret;
}

int main()
{
   printf("%ld\n", kvm_hypercall0(KVM_HC_VM_NUM));

   return 0;
}
