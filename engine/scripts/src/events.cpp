#include <api.h>
#include <include/event_loop.hpp>

static std::array<Events<>, 2> events;

PUBLIC(void event_loop())
{
	api::print("Entering event loop...\n");
	while (true)
	{
		for (auto& ev : events)
			ev.consume_work();
		halt();
	}
}

PUBLIC(bool add_work(const Events<>::Work* work))
{
	EXPECT(work != nullptr);
	for (auto& ev : events)
		if (ev.add(*work))
			return true;
	return false;
}
