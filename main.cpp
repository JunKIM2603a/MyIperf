#include "CLIHandler.h"
#include "TestController.h"
#include "Logger.h"

/**
 * @brief The main entry point for the IPEFTC application.
 *
 * @param argc The number of command-line arguments.
 * @param argv An array of command-line argument strings.
 * @return 0 on successful execution, non-zero otherwise.
 */
int main(int argc, char* argv[]) {
    // This initial log goes to cerr because the logger is not yet started.
    std::cerr << "DEBUG: main() started." << std::endl;

    // Iterate through all command-line arguments.
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            CLIHandler::printHelp();
            exit(0);
        }
    }

    // Create the main controller for managing tests.
    TestController controller;
    // Create a command-line handler and link it with the controller.
    CLIHandler cli(controller);

    // Run the command-line handler to parse arguments and start the test.
    // Logger::start() is called inside cli.run()
    cli.run(argc, argv);

    // Wait for the test to complete before exiting the application.
    // This ensures all asynchronous operations have a chance to finish.
    
    controller.getTestCompletionFuture().wait();
    

    Logger::log("Info: IPEFTC application finished.");
    // Stop the logger service, ensuring all messages are flushed.
    Logger::stop();
    
    // This final log goes to cerr because the logger is stopped.
    std::cerr << "DEBUG: main() is about to exit." << std::endl;

    return 0;
}