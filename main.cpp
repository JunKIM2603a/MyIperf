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
    // Start the asynchronous logger service.
    Logger::start();
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
    
    return 0;
}