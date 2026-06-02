#include "cli.hpp"
#include "utility.hpp"

#include <cstdlib>
#include <csignal>
#include <exception>
#include <iostream>

int main(int argc, char **argv) {
    try {
#if !defined(_WIN32)
        std::signal(SIGPIPE, SIG_IGN);
#endif
        setEnvironmentVariable("RUST_LOG", "error");
        int exitCode = runCli(argc, argv);
        std::cout.flush();
        std::cerr.flush();
        std::_Exit(exitCode);
    } catch (const std::exception &exception) {
        std::cerr << "error: " << exception.what() << "\n";
        std::cerr.flush();
        std::_Exit(1);
    }
}
