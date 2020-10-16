#include "engine.hpp"

__attribute__((noinline)) void halt()
{
	asm("ebreak" ::: "memory");
}

static_assert(ECALL_FARCALL == 104, "The farcall syscall number is hard-coded in assembly");
asm(".global farcall_helper\n"
"farcall_helper:\n"
"	li a7, 104\n"
"	ecall\n"
"");
