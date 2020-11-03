#pragma once

template <typename Expr>
inline void expect_check(Expr expr, const char* strexpr,
	const char* file, int line, const char* func)
{
	if (UNLIKELY(!expr())) {
		asm ("" ::: "memory"); // prevent dead-store optimization
		syscall(ECALL_ASSERT_FAIL, (long) strexpr, (long) file, (long) line, (long) func);
		__builtin_unreachable();
	}
}
#define EXPECT(expr) \
	api::expect_check([&] { return (expr); }, #expr, __FILE__, __LINE__, __FUNCTION__)

template <typename... Args>
inline void print(Args&&... args)
{
	char buffer[500];
	auto res = strf::to(buffer) (std::forward<Args> (args)...);
	psyscall(ECALL_WRITE, buffer, res.ptr - buffer);
}

template <typename T>
inline long measure(const char* testname, T testfunc)
{
	return syscall(ECALL_MEASURE, (long) testname, (long) static_cast<void(*)()>(testfunc));
}

extern "C" void (*farcall_helper) ();
extern "C" void (*direct_farcall_helper) ();

template <typename Func>
struct FarCall {
	const uint32_t mhash;
	const uint32_t fhash;

	constexpr FarCall(const char* m, const char* f)
		: mhash(crc32(m)), fhash(crc32(f)) {}

	template <typename... Args>
	auto operator() (Args&&... args) const {
		static_assert( std::is_invocable_v<Func, Args...> );
		using Ret = std::result_of<Func>;
		using FCH = Ret(uint32_t, uint32_t, Args... args);

		auto* fch = reinterpret_cast<FCH*> (&farcall_helper);
		return fch(mhash, fhash, args...);
	}
};

template <typename Func>
struct ExecuteRemotely {
	const uint32_t mhash;
	const Func func;

	constexpr ExecuteRemotely(const char* m, Func f) : mhash(crc32(m)), func(f) {}
	constexpr ExecuteRemotely(uint32_t m, Func f) : mhash(m), func(f) {}

	template <typename... Args>
	auto operator() (Args&&... args) const {
		static_assert( std::is_invocable_v<Func, Args...> );
		using Ret = decltype(func(args...));
		using FCH = Ret(uint32_t, Func, Args... args);

		auto* fch = reinterpret_cast<FCH*> (&direct_farcall_helper);
		return fch(mhash, func, args...);
	}
};

template <typename T>
inline long interrupt(uint32_t mhash, uint32_t fhash, T argument)
{
	asm ("" ::: "memory"); // avoid dead-store optimzation
	return syscall(ECALL_INTERRUPT, mhash, fhash, (long) argument);
}
inline long interrupt(uint32_t mhash, uint32_t fhash)
{
	return syscall(ECALL_INTERRUPT, mhash, fhash);
}
#define INTERRUPT(mach, function, ...) \
		api::interrupt(crc32(mach), crc32(function), ## __VA_ARGS__)

inline uint32_t current_machine()
{
	return syscall(ECALL_MACHINE_HASH);
}
#define RUNNING_ON(mach) (api::current_machine() == crc32(mach))

inline void each_frame_helper(int count, int reason)
{
	for (int i = 0; i < count; i++)
		microthread::wakeup_one_blocked(reason);
}
inline void wait_next_tick()
{
	microthread::block(REASON_FRAME);
}
template <typename T, typename... Args>
inline void each_tick(const T& func, Args&&... args)
{
	static bool init = false;
	if (!init) {
		init = true;
		(void) syscall(ECALL_EACH_FRAME, (long) each_frame_helper, REASON_FRAME);
	}
	microthread::oneshot(func, std::forward<Args> (args)...);
}

template <int G, int I, typename F>
struct GroupCall {
	static constexpr size_t GROUP_SIZE = 64;
	static_assert(I >= 0 && I < GROUP_SIZE, "Index must be inside the group");
	static constexpr uintptr_t addr = FUNCTION_GROUP_AREA + ((G * GROUP_SIZE) | I) * 8;
	static_assert(addr >= FUNCTION_GROUP_AREA, "Address must be in the memory range");

	static constexpr auto group() { return G; }
	static constexpr auto index() { return I; }

	template <typename... Args>
	auto operator() (Args&&... args) const {
		static_assert( std::is_invocable_v<F, Args...> );
		const auto& func = *(F*) addr;
		return func(std::forward<Args>(args)...);
	}
};

template <int G, int I, typename F = void(), typename... Args>
inline auto groupcall(Args&&... args) {
	return GroupCall<G, I, F> {} (std::forward<Args>(args)...);
}

inline bool check_group(int gid, std::initializer_list<int> indices)
{
	uint64_t bits = 0x0;
	for (const int idx : indices) {
		bits |= 1 << idx;
	}
	if constexpr(sizeof(void*) == 8)
		return syscall(ECALL_CHECK_GROUP, gid, bits);
	else // in 32-bit we pass the 64-bit integer in 2 registers
		return syscall(ECALL_CHECK_GROUP, gid, bits >> 32, bits);
}

inline void Game::exit()
{
	(void) syscall(ECALL_GAME_EXIT);
}

using timer_callback = void (*) (int, void*);
inline Timer timer_periodic(float time, float period, timer_callback callback, void* data, size_t size)
{
	static GroupCall<2, 1, int(float, float, timer_callback, void*, size_t)> periodic_timer;

	return {periodic_timer(time, period, callback, data, size)};
}
inline Timer timer_periodic(float period, timer_callback callback, void* data, size_t size)
{
	return timer_periodic(period, period, callback, data, size);
}
inline Timer Timer::periodic(float time, float period, Function<void(Timer)> callback)
{
	return timer_periodic(time, period,
		(timer_callback) [] (int id, void* data) {
			(*(decltype(&callback)) data) ({id});
		}, &callback, sizeof(callback));
}
inline Timer Timer::periodic(float period, Function<void(Timer)> callback)
{
	return Timer::periodic(period, period, std::move(callback));
}
inline Timer timer_oneshot(float time, timer_callback callback, void* data, size_t size)
{
	return timer_periodic(time, 0.0f, std::move(callback), data, size);
}
inline Timer Timer::oneshot(float time, Function<void(Timer)> callback)
{
	return timer_oneshot(time,
		(timer_callback) [] (int id, void* data) {
			(*(decltype(&callback)) data) ({id});
		}, &callback, sizeof(callback));
}

inline void Timer::stop() const {
	static GroupCall<2, 0, void(int)> stop_timer;
	stop_timer(this->id);
}

inline long sleep(float seconds) {
	const int tid = microthread::gettid();
	Timer::oneshot(seconds, [tid] (auto) {
		microthread::unblock(tid);
	});
	return microthread::block();
}

/** Maffs **/

inline float sin(float x) {
	return fsyscallf(ECALL_SINF, x);
}
inline float cos(float x) {
	return fsyscallf(ECALL_SINF, x + PI/2);
}
inline float rand(float a, float b) {
	return fsyscallf(ECALL_RANDF, a, b);
}
inline float smoothstep(float a, float b, float x) {
	return fsyscallf(ECALL_SMOOTHSTEP, a, b, x);
}
inline float length(float dx, float dy) {
	return fsyscallf(ECALL_VEC_LENGTH, dx, dy);
}
inline vec2 rotate_around(float dx, float dy, float angle) {
	const auto [x, y] = fsyscallff(ECALL_VEC_LENGTH, dx, dy, angle);
	return {x, y};
}
inline vec2 normalize(float dx, float dy) {
	const auto [x, y] = fsyscallff(ECALL_VEC_NORMALIZE, dx, dy);
	return {x, y};
}

inline float vec2::length() const
{
	return api::length(this->x, this->y);
}
inline vec2 vec2::rotate(float angle) const
{
	return rotate_around(this->x, this->y, angle);
}
inline vec2 vec2::normalized() const
{
	return api::normalize(this->x, this->y);
}
inline void vec2::normalize()
{
	*this = api::normalize(this->x, this->y);
}
