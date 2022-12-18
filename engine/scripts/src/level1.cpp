#include "events.hpp"
#include <api.h>
using namespace api;
extern void do_threads_stuff();
extern void do_remote_stuff();

static inline void
my_dynamic_call(long val, float fval, const std::string& str)
{
	DYNCALL("Test::my_dynamic_call", val, fval, str);
}

/* This is the function that gets called at the start.
   See: engine/src/main.cpp */
PUBLIC(void start())
{
	/* 'print' is implemented in api_impl.h, and it makes a system
	   call into the engine, which then writes to the terminal. */
	print("Hello world!\n\n");

	print("** Fully dynamic system calls **\n");

	my_dynamic_call(1234, 5678.0, "nine-ten-eleven-twelve!");

	measure("Benchmark overhead", [] {});

	measure(
		"Dynamic call (no arguments)",
		[]
		{
			DYNCALL("Test::void");
		});

	measure(
		"Dynamic call (4x arguments)",
		[]
		{
			DYNCALL("Test::void", 1234, 5678.0, 4321, 8765.0);
		});

	print("** Remote **\n");

	/* Do some remote function calls. */
	do_remote_stuff();

	print("** Threads **\n");

	/* Test-run some micro-threads. */
	do_threads_stuff();
}

int main()
{
	print("Hello from Level 1 main()\n");
	return 0;
}