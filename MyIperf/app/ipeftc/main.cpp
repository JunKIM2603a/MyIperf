#include "CLIHandler.h"
#include "myiperf/Logger.h"
#include "myiperf/TestController.h"

#include <iostream>
#include <string>

/**
 * @brief The main entry point for the IPEFTC application.
 *
 * This function initializes the application, parses command-line arguments,
 * starts the appropriate test (client or server), and waits for the test
 * to complete before shutting down.
 *
 * @param argc The number of command-line arguments.
 * @param argv An array of command-line argument strings.
 * @return 0 on successful execution, non-zero otherwise.
 */
int main(int argc, char* argv[]) {
    std::cout << "=============== START ==============\n" << std::endl;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            CLIHandler::printHelp();
            return 0;
        }
    }

    Logger::log("Info: IPEFTC (IPerf Test Client/Server) application starting.");

    TestController controller;
    CLIHandler cli(controller);

    if (!cli.run(argc, argv)) {
        Logger::stop();
        return 1;
    }

    Logger::log("Info: Waiting for the test to complete...");
    controller.getTestCompletionFuture().wait();

    controller.stopTest();

    Logger::log("Info: IPEFTC application finished.");
    Logger::stop();

    std::cout << "=============== END ==============\n" << std::endl;
    return controller.completedSuccessfully() ? 0 : 1;
}
