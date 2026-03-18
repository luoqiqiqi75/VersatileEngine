// core/terminal/main.cpp — ve_terminal: interactive Node console (thin wrapper)

#include "ve/service/terminal.h"
#include "ve/core/node.h"
#include <iostream>
#include <string>

int main()
{
    std::cout << "ve_terminal — interactive Node console (v3)\n"
              << "type 'help' for commands\n\n";

    ve::Terminal term;
    term.setOutput([](const std::string& text) {
        std::cout << text;
        std::cout.flush();
    });

    std::string line;
    while (true) {
        std::cout.clear();
        std::cout << term.prompt() << std::flush;
        if (!std::getline(std::cin, line)) break;
        if (!term.execute(line)) break;
    }

    std::cout << "bye\n";
    return 0;
}
