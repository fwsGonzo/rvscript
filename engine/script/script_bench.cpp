#include "script.hpp"
using gaddr_t = Script::gaddr_t;

inline timespec time_now();
inline long nanodiff(timespec start_time, timespec end_time);

template <int ROUNDS = 2000>
inline long perform_test(Script::machine_t& machine, gaddr_t func)
{
	const auto regs = machine.cpu.registers();
	const auto counter = machine.instruction_counter();
	const auto max_counter = machine.max_instructions();
	// this is a very hacky way of avoiding blowing up the stack
	// because vmcall() resets the stack pointer on each call
	auto old_stack = machine.memory.stack_initial();
	timespec t0 = {}, t1 = {};
	try {
		machine.memory.set_stack_initial(machine.cpu.reg(riscv::REG_SP) - 2048);
		// Warmup once
		machine.vmcall(func);
		asm("" : : : "memory");
		t0 = time_now();
		asm("" : : : "memory");
		for (int i = 0; i < ROUNDS; i++) {
			machine.vmcall(func);
		}
		asm("" : : : "memory");
		t1 = time_now();
		asm("" : : : "memory");
	} catch (...) {}
	machine.cpu.registers() = regs;
	machine.set_instruction_counter(counter);
	machine.set_max_instructions(max_counter);
	machine.memory.set_stack_initial(old_stack);
	return nanodiff(t0, t1);
}

long Script::vmbench(gaddr_t address, size_t ntimes)
{
	static constexpr size_t TIMES = 2000;

	std::vector<long> results;
	for (size_t i = 0; i < ntimes; i++)
	{
		perform_test<1>(*m_machine, address); // warmup
		results.push_back( perform_test<TIMES>(*m_machine, address) / TIMES );
	}
	return finish_benchmark(results);
}

long Script::benchmark(std::function<void()> callback, size_t TIMES)
{
	std::vector<long> results;
	for (int i = 0; i < 30; i++)
	{
		callback();
		asm("" : : : "memory");
		auto t0 = time_now();
		asm("" : : : "memory");
		for (size_t j = 0; j < TIMES; j++) {
			callback();
		}
		asm("" : : : "memory");
		auto t1 = time_now();
		asm("" : : : "memory");
		results.push_back( nanodiff(t0, t1) / TIMES );
	}
	return finish_benchmark(results);
}

long Script::finish_benchmark(std::vector<long>& results)
{
	std::sort(results.begin(), results.end());
	const long median = results[results.size() / 2];
	const long lowest = results[0];
	const long highest = results[results.size()-1];

	fmt::print("> median {}ns  \t\tlowest: {}ns     \thighest: {}ns\n",
			median, lowest, highest);
	return median;
}


timespec time_now()
{
	timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return t;
}
long nanodiff(timespec start_time, timespec end_time)
{
	return (end_time.tv_sec - start_time.tv_sec) * (long)1e9 + (end_time.tv_nsec - start_time.tv_nsec);
}
