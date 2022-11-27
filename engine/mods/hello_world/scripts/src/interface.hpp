#pragma once
#include <map>
#include <string>

struct Gameplay {
    bool action = false;

    std::map<unsigned, std::string> strings;

    void set_action(bool a);
    bool get_action();

    void set_string(unsigned, const std::string&);
    void print_string(unsigned);

    void do_nothing();
};
extern Gameplay gameplay_state;
