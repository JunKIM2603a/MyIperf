#include "CLIHandler.h"
#include "TestController.h"
#include "Logger.h"

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
    std::cerr << "DEBUG: Entering main()\n";


    // Iterate through all command-line arguments.
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            CLIHandler::printHelp();
            exit(0);
        }
    }

    // Start the asynchronous logger service.
    // Logger::start();
    Logger::log("Info: IPEFTC (IPerf Test Client/Server) application starting.");

    // Create the main controller for managing tests.
    TestController controller;
    // Create a command-line handler and link it with the controller.
    CLIHandler cli(controller);

    // Run the command-line handler to parse arguments and start the test.
    cli.run(argc, argv);

    // Wait for the test to complete before exiting the application.
    // This ensures all asynchronous operations have a chance to finish.
    Logger::log("Info: Waiting for the test to complete...");
    controller.getTestCompletionFuture().wait();

    Logger::log("Info: IPEFTC application finished.");
    // Stop the logger service, ensuring all messages are flushed.
    Logger::stop();
    
    std::this_thread::sleep_for(std::chrono::seconds(10)); // ADDED DELAY for debug pipe communication
    std::cout << "=============== END ==============\n"<< std::endl;
    return 0;
}