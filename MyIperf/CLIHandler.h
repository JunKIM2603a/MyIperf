#pragma once

#include "TestController.h"
#include "ConfigParser.h"
#include <string>
#include <vector>
#include <map>

/**
 * @class CLIHandler
 * @brief Handles command-line argument parsing and initiates tests based on user input.
 *
 * This class is responsible for interpreting the command-line arguments provided
 * by the user, setting up the test configuration, and starting the appropriate
 * test mode (client or server) via the TestController.
 */
class CLIHandler {
public:
    /**
     * @brief Constructs a CLIHandler.
     * @param controller A reference to the TestController that will manage the test execution.
     */
    CLIHandler(TestController& controller);

    /**
     * @brief Starts the command-line interface processing.
     * @param argc The number of command-line arguments.
     * @param argv An array of command-line argument strings.
     */
    void run(int argc, char* argv[]);

    /**
     * @brief Prints the help message with usage instructions to the console.
     */
    static void printHelp();

private:
    /**
     * @brief Parses the command-line arguments to create a configuration.
     * @param argc The number of command-line arguments.
     * @param argv An array of command-line argument strings.
     * @return A Config object populated with the settings from the arguments.
     * @throws std::runtime_error if arguments are invalid or missing.
     */
    Config parseArgs(int argc, char* argv[]);

    /**< A reference to the TestController to manage the test. */
    TestController& testController;
};