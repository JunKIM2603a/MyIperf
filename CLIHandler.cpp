#include "CLIHandler.h"
#include "Logger.h"
#include <iostream>
#include <algorithm> // Required for std::transform
#include "Logger.h"

/**
 * @brief Constructs the CLIHandler.
 * @param controller A reference to the TestController to be managed.
 */
CLIHandler::CLIHandler(TestController& controller) : testController(controller) {}

/**
 * @brief Runs the command-line interface.
 * Parses arguments and starts the test accordingly.
 * @param argc Argument count.
 * @param argv Argument values.
 */
void CLIHandler::run(int argc, char* argv[]) {
    // If no arguments are provided, display help and exit.
    if (argc < 2) {
        printHelp();
        return;
    }

    try {
        // Parse command-line arguments to get the test configuration.
        Config config = parseArgs(argc, argv);
        // Start the test with the parsed configuration.
        testController.startTest(config);

        // If in server mode, wait for the test to complete before shutting down.
        if (config.getMode() == Config::TestMode::SERVER) {
            Logger::log("Info: Server is running. Waiting for the test to complete...");
            // Block until the test completion is signaled.
            testController.getTestCompletionFuture().get();
            testController.stopTest();
            Logger::log("Info: Server test finished. Shutting down.");
        }
    } catch (const std::exception& e) {
        // Log any errors that occur during setup or execution.
        Logger::log("Error: " + std::string(e.what()));
        printHelp();
    }
}

/**
 * @brief Parses command-line arguments to configure the test.
 * @param argc Argument count.
 * @param argv Argument values.
 * @return A Config object with the parsed settings.
 */
Config CLIHandler::parseArgs(int argc, char* argv[]) {
    Config config;
    std::string mode = "";
    std::string configFilePath = "";

    // Iterate through all command-line arguments.
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            printHelp();
            exit(0);
        } else if (arg == "--mode" && i + 1 < argc) {
            mode = argv[++i];
            // Convert mode to uppercase for case-insensitive comparison.
            std::transform(mode.begin(), mode.end(), mode.begin(), ::toupper);
        } else if (arg == "--config" && i + 1 < argc) {
            configFilePath = argv[++i];
        } else if (arg == "--target" && i + 1 < argc) {
            config.setTargetIP(argv[++i]);
        } else if (arg == "--port" && i + 1 < argc) {
            config.setPort(std::stoi(argv[++i]));
        } else if (arg == "--packet-size" && i + 1 < argc) {
            config.setPacketSize(std::stoi(argv[++i]));
        } else if (arg == "--num-packets" && i + 1 < argc) {
            config.setNumPackets(std::stoi(argv[++i]));
        } else if (arg == "--interval-ms" && i + 1 < argc) {
            config.setSendIntervalMs(std::stoi(argv[++i]));
        } else {
            throw std::runtime_error("Unknown argument or missing value: " + arg);
        }
    }

    // Set the test mode (Client or Server).
    if (mode == "CLIENT") {
        config.setMode(Config::TestMode::CLIENT);
    } else if (mode == "SERVER") {
        config.setMode(Config::TestMode::SERVER);
    } else {
        throw std::runtime_error("Error: Mode (--mode) must be specified as either 'client' or 'server'.");
    }

    // If a configuration file is specified, load it.
    if (!configFilePath.empty()) {
        ConfigParser parser(configFilePath);
        if (parser.load()) {
            Config fileConfig = parser.getConfig();
            // Command-line arguments override settings from the config file.
            if (config.getPacketSize() == 1024) config.setPacketSize(fileConfig.getPacketSize());
            if (config.getTargetIP() == "127.0.0.1") config.setTargetIP(fileConfig.getTargetIP());
            if (config.getPort() == 5201) config.setPort(fileConfig.getPort());
        } else {
            throw std::runtime_error("Error: Failed to load configuration from file: " + configFilePath);
        }
    }
    return config;
}

/**
 * @brief Prints the command-line usage instructions.
 */
void CLIHandler::printHelp() const {
    std::cout << "Usage: ipeftc --mode <client|server> [options]\n"
              << "\nOptions:\n"
              << "  --mode <client|server>    Specify the operating mode (required).\n"
              << "  --config <path>           Path to a JSON configuration file.\n"
              << "  --target <ip_address>     Target IP address for the client.\n"
              << "  --port <port_number>      Port number for the connection.\n"
              << "  --packet-size <bytes>     Size of data packets in bytes.\n"
              << "  --num-packets <count>     Number of packets to send (0 for unlimited).\n"
              << "  --interval-ms <ms>        Delay between sends in milliseconds.\n"
              << "  -h, --help                Display this help message and exit.\n";
}
