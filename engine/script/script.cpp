#include "script.hpp"
using gaddr_t = Script::gaddr_t;

#include <libriscv/native_heap.hpp>
#include <libriscv/threads.hpp>
#include <libriscv/util/crc32.hpp>
#include <fstream> // Windows doesn't implement C getline()
#include <sstream>
#include "machine/include_api.hpp"

// the shared area is read-write for the guest
using riscv::crc32;

Script::Script(const machine_t& smach, void* userptr, const std::string& name, bool debug)
	: m_source_machine(smach), m_userptr(userptr), m_name(name),
	  m_hash(crc32(name.c_str())), m_is_debug(debug)
{
	this->reset();
}
Script::~Script() {}

bool Script::reset()
{
	try {
		// Fork the source machine into m_machine */
		riscv::MachineOptions<MARCH> options {
			.memory_max = MAX_MEMORY,
#ifdef RISCV_BINARY_TRANSLATION
			.translate_blocks_max = (m_is_debug ? 0u : 4000u),
#endif
		};
		m_machine.reset(new machine_t(m_source_machine, options));

	} catch (std::exception& e) {
		fmt::print(">>> Exception during initialization: {}\n", e.what());
		throw;
	}
	// setup system calls and traps
	this->machine_setup();

	return true;
}

void Script::add_shared_memory()
{
	const int heap_pageno   = heap_area() >> riscv::Page::SHIFT;

	static int counter = 0;
	const int stack_pageno  = heap_pageno - 2 - counter;
	// Separate each stack base address by 16 pages, for each machine.
	// This will make it simple to mirror stacks when calling remotely.
	counter = (counter + 16) % 256;

	auto& mem = machine().memory;
	mem.set_stack_initial((gaddr_t) stack_pageno << riscv::Page::SHIFT);
	// Install our shared guard-page around the shared-
	// memory area, put the shared page in the middle.
	auto& guard_page = riscv::Page::guard_page();
	// this separates heap and stack
	mem.install_shared_page(stack_pageno,    guard_page);
}

bool Script::initialize()
{
	// clear some state belonging to previous initialization
	this->m_tick_event = 0;
	// run through the initialization
	try {
		machine().simulate(MAX_INSTRUCTIONS);

		if (UNLIKELY(machine().instruction_counter() >= MAX_INSTRUCTIONS)) {
			fmt::print(stderr, ">>> Exception: Ran out of instructions\n");
			return false;
		}
	} catch (riscv::MachineException& me) {
		fmt::print(stderr,
			">>> Machine exception {}: {} (data: {:#x})\n",
			me.type(), me.what(), me.data());
#ifdef RISCV_DEBUG
		m_machine->print_and_pause();
#endif
		return false;
	} catch (std::exception& e) {
		fmt::print(stderr, ">>> Exception: {}\n", e.what());
		return false;
	}
    return true;
}
void Script::machine_setup()
{
	machine().set_userdata<Script>(this);
	machine().set_printer(
		[] (const char* p, size_t len) {
			fmt::print(stderr, "{}", std::string_view{p, len});
		});
	// add system call interface
	machine().setup_native_heap(HEAP_SYSCALLS_BASE, heap_area(), MAX_HEAP);
	machine().setup_native_memory(MEMORY_SYSCALLS_BASE, true);
	machine().setup_native_threads(THREADS_SYSCALL_BASE);
	machine().on_unhandled_syscall =
		[] (machine_t&, int number) {
			fmt::print(stderr, "Unhandled system call: {}\n", number);
		};

	// we need to pass the .eh_frame location to a supc++ function,
	// if C++ RTTI and Exceptions is enabled
	machine().cpu.reg(11) = machine().memory.resolve_section(".eh_frame");
	// install the shared memory area
	this->add_shared_memory();
}
void Script::handle_exception(gaddr_t address)
{
	try {
		throw; // re-throw
	}
	catch (const riscv::MachineTimeoutException& e) {
		this->handle_timeout(address);
		return; // NOTE: might wanna stay
	}
	catch (const riscv::MachineException& e) {
		fmt::print(stderr, "Script::call exception: {} (data: {:#x})\n",
			e.what(), e.data());
		fmt::print(stderr, ">>> Machine registers:\n[PC\t{:08x}] {}\n",
			(long) machine().cpu.pc(),
			machine().cpu.registers().to_string());
	}
	catch (const std::exception& e) {
		fmt::print(stderr, "Script::call exception: {}\n",
			e.what());
	}
	fmt::print(stderr, "Program page: {}\n",
		machine().memory.get_page_info(machine().cpu.pc()));
	fmt::print(stderr, "Stack page: {}\n",
		machine().memory.get_page_info(machine().cpu.reg(2)));
	// close all threads
	auto& mt = machine().threads();
	while (mt.get_thread()->tid != 0) {
		auto* thread = mt.get_thread();
		fmt::print(stderr, "Script::call Closing running thread: {}\n",
			thread->tid);
		thread->exit();
	}

	auto callsite = machine().memory.lookup(address);
	fmt::print(stderr, "Function call: {}\n", callsite.name);
	this->print_backtrace(address);
}
void Script::handle_timeout(gaddr_t address)
{
	this->m_budget_overruns ++;
	auto callsite = machine().memory.lookup(address);
	fmt::print(stderr, "Script::call hit max instructions for: {}"
		" (Overruns: {})\n", callsite.name, m_budget_overruns);
	// check if we need to suspend a thread
	auto& mt = machine().threads();
	auto* thread = mt.get_thread();
	if (thread->tid != 0) {
		// try to do the right thing here
		if (thread->block_reason != 0) {
			thread->block(thread->block_reason);
		} else {
			thread->suspend();
		}
		// resume some other thread
		mt.wakeup_next();
	}
}
void Script::print_backtrace(const gaddr_t addr)
{
	machine().memory.print_backtrace(
		[] (std::string_view line) {
			fmt::print("-> {}\n", line);
		});
	auto origin = machine().memory.lookup(addr);
	fmt::print("-> [-] 0x{:016x} + 0x{:03x}: {}\n",
			origin.address, origin.offset, origin.name);
}

void Script::hash_public_api_symbols(std::string_view contents)
{
	std::stringstream infile {std::string(contents)};

	for (std::string line; infile >> line; )
	{
		m_public_api.insert({
			crc32(line.c_str()), machine().address_of(line.c_str())
		});
	}
}
void Script::hash_public_api_symbols_file(const std::string& file)
{
	/* Ignore symbol file when not specified */
	if (file.empty()) return;

	std::ifstream infile(file);
	if (!infile) {
		fmt::print(stderr,
			">>> Could not find symbols file: {}, ignoring...\n",
			file);
		return;
	}

	std::string str((std::istreambuf_iterator<char>(infile)),
	                 std::istreambuf_iterator<char>());

	this->hash_public_api_symbols(str);
}

gaddr_t Script::address_of(const std::string& name) const {
	return machine().address_of(name.c_str());
}
std::string Script::symbol_name(gaddr_t address) const
{
	auto callsite = machine().memory.lookup(address);
	return callsite.name;
}
gaddr_t Script::api_function_from_hash(uint32_t hash) {
	auto it = m_public_api.find(hash);
	if (it != m_public_api.end()) return it->second;
	return 0;
}

void Script::each_tick_event()
{
	if (this->m_tick_event == 0)
		return;
	auto& mt = machine().threads();
	assert(mt.get_thread()->tid == 0 && "Avoid clobbering regs");

	int count = 0;
	for (auto* thread : mt.m_blocked)
	{
		if (thread->block_reason == this->m_tick_block_reason)
			count++;
	}
	this->preempt(this->m_tick_event, (int) count, (int) this->m_tick_block_reason);
	assert(mt.get_thread()->tid == 0 && "Avoid clobbering regs");
}

void Script::set_dynamic_call(const std::string& name, ghandler_t handler)
{
	const uint32_t hash = crc32(name.c_str(), name.size());
	//fmt::print("{}: {:x}\n", name, hash);
	auto it = m_dynamic_functions.find(hash);
	if (it != m_dynamic_functions.end()) {
		fmt::print("Dynamic function with hash {:#08x} already exists\n",
			hash);
		throw std::runtime_error("set_dynamic_call failed: Hash collision");
	}
	m_dynamic_functions.emplace(hash, std::move(handler));
}
void Script::reset_dynamic_call(const std::string& name, ghandler_t handler)
{
	const uint32_t hash = crc32(name.c_str(), name.size());
	m_dynamic_functions.erase(hash);
	if (handler != nullptr) {
		set_dynamic_call(name, std::move(handler));
	}
}
void Script::set_dynamic_calls(std::vector<std::pair<std::string, ghandler_t>> vec)
{
	for (const auto& pair : vec) {
		set_dynamic_call(pair.first, std::move(pair.second));
	}
}

void Script::dynamic_call(uint32_t hash)
{
	auto it = m_dynamic_functions.find(hash);
	if (LIKELY(it != m_dynamic_functions.end())) {
		it->second(*this);
	} else {
		fmt::print("Unable to find dynamic function with hash: {:#08x}\n",
			hash);
		throw std::runtime_error("Unable to find dynamic function");
	}
}

gaddr_t Script::guest_alloc(gaddr_t bytes) {
	return machine().arena().malloc(bytes);
}
void Script::guest_free(gaddr_t addr) {
	machine().arena().free(addr);
}


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
	machine.memory.set_stack_initial(machine.cpu.reg(riscv::REG_SP) - 2048);
	asm("" : : : "memory");
	auto t0 = time_now();
	asm("" : : : "memory");
	for (int i = 0; i < ROUNDS; i++) {
		machine.vmcall(func);
	}
	asm("" : : : "memory");
	auto t1 = time_now();
	asm("" : : : "memory");
	machine.cpu.registers() = regs;
	machine.reset_instruction_counter();
	machine.increment_counter(counter);
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
