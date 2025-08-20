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
        exit(0);
    }

    try {
        // Parse command-line arguments to get the test configuration.
        Config config = parseArgs(argc, argv);
        // Start the test with the parsed configuration.
        testController.startTest(config);

        // If in server mode, wait for the test to complete before shutting down.
        if (config.getMode() == Config::TestMode::SERVER) {
            Logger::log("Info: Server is running. Waiting for the test to complete...");
            // Block until the test completion is signaled by TestController
            {
                std::unique_lock<std::mutex> lock(testController.m_cliBlockMutex);
                testController.m_cliBlockCv.wait(lock, [&]{ return testController.m_cliBlockFlag.load(); });
            }
            testController.stopTest();
            Logger::log("Info: Server test finished. Shutting down.");
        }
    } catch (const std::exception& e) {
        // Log any errors that occur during setup or execution.
        Logger::log("Error: " + std::string(e.what()));
        printHelp();
        exit(0);
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
            // This is now handled in main.cpp, so we just ignore it here.
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
void CLIHandler::printHelp() {
    std::cout << "MyIperf - A simple network performance testing tool\n\n" \
              << "DESCRIPTION:\n"
              << "  This tool measures network throughput between a client and a server. \n"
              << "  It works by sending a configured number of packets of a specific size \n"
              << "  from a client to a server and measuring the data transfer rate.\n\n"
              << "  The client and server exchange statistics at the end of the test, \n"
              << "  so both sides will display a full report including the remote peer's perspective.\n\n"
              << "USAGE:\n"
              << "  ipeftc --mode <client|server> [options]\n\n"
              << "OPTIONS:\n"
              << "  --mode <client|server>    Specify the operating mode (required).\n"
              << "  --config <path>           Path to a JSON configuration file. Command-line options will override file settings.\n"
              << "  --target <ip_address>     Target IP address for the client (e.g., 192.168.1.100).\n"
              << "  --port <port_number>      Port number for the connection (e.g., 5201).\n"
              << "  --packet-size <bytes>     Size of data packets in bytes (includes header).\n"
              << "  --num-packets <count>     Number of packets to send (0 for unlimited until interrupted).\n"
              << "  --interval-ms <ms>        Delay between sending packets in milliseconds (0 for continuous send).\n"
              << "  -h, --help                Display this help message and exit.\n\n"
              << "UNDERSTANDING THE FINAL REPORT:\n"
              << "  The report is split into two main sections:\n"
              << "  1. Local Stats: This machine's perspective.\n"
              << "     - If CLIENT: Shows how much data was SENT.\n"
              << "     - If SERVER: Shows how much data was RECEIVED.\n"
              << "  2. Remote Stats: The other machine's perspective, as reported by it.\n"
              << "     - If CLIENT: Shows the SERVER's stats (how much it RECEIVED).\n"
              << "     - If SERVER: Shows the CLIENT's stats (how much it SENT).\n\n"
              << "  Key Metrics:\n"
              << "  - Total Bytes: Total bytes transferred, including packet headers.\n"
              << "  - Total Packets: Total number of packets transferred.\n"
              << "  - Duration (s): The total time taken for the data transfer phase of the test.\n"
              << "  - Throughput (Mbps): The calculated data transfer rate in Megabits per second.\n"
              << "                       Formula: (Total Bytes * 8) / (Duration * 1,000,000)\n"
              << "  - Checksum/Sequence Errors: Indicate potential packet corruption or loss during transit.\n";
}

