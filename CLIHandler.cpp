#include "CLIHandler.h"
#include "Logger.h"
#include <iostream>
#include <algorithm> // Required for std::transform
#include <vector> // Required for std::vector

/**
 * @brief Constructs the CLIHandler.
 * @param controller A reference to the TestController to be managed.
 */
CLIHandler::CLIHandler(TestController& controller) : testController(controller) {
    // No logging here, logger is not started yet.
}

/**
 * @brief Runs the command-line interface.
 * Parses arguments and starts the test accordingly.
 * @param argc Argument count.
 * @param argv Argument values.
 */
void CLIHandler::run(int argc, char* argv[]) {
    std::cerr << "DEBUG: CLIHandler::run called." << std::endl;

    if (argc < 2) {
        printHelp();
        exit(0);
    }

    Config config;
    try {
        // First, parse the arguments. This can throw.
        config = parseArgs(argc, argv);
    } catch (const std::exception& e) {
        // If parsing fails, the logger is not running. Print directly to stderr.
        std::cerr << "Error: " << e.what() << std::endl;
        printHelp();
        exit(1); // Use a non-zero exit code for errors.
    }

    // Now that we have a valid config, we can start the logger.
    Logger::start(config);
    

    try {
        // With the logger running, start the test.
        testController.startTest(config);
        

        // If in server mode, wait for the test to complete before shutting down.
        if (config.getMode() == Config::TestMode::SERVER) {
            Logger::log("Info: Server is running. Waiting for the test to complete...");
            {
                std::unique_lock<std::mutex> lock(testController.m_cliBlockMutex);
                
                testController.m_cliBlockCv.wait(lock, [&]{ return testController.m_cliBlockFlag.load(); });
                
            }
            testController.stopTest();
            Logger::log("Info: Server test finished. Shutting down.");
        }
    } catch (const std::exception& e) {
        // The logger is running, so we can use it for test-related errors.
        Logger::log("Error: An exception occurred during the test: " + std::string(e.what()));
    }
    
}


/**
 * @brief Parses command-line arguments to configure the test.
 * @param argc Argument count.
 * @param argv Argument values.
 * @return A Config object with the parsed settings.
 */
Config CLIHandler::parseArgs(int argc, char* argv[]) {
    Config config; // Start with default config
    std::string mode = "";
    std::string configFilePath = "";

    // First pass: find the config file path
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--config" && i + 1 < argc) {
            configFilePath = argv[i + 1];
            break;
        }
    }

    // Load from config file if provided
    if (!configFilePath.empty()) {
        ConfigParser parser(configFilePath);
        if (parser.load()) {
            config = parser.getConfig();
        } else {
            throw std::runtime_error("Failed to load configuration from file: " + configFilePath);
        }
    }

    // Second pass: parse command-line args, which will override file/default settings
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            // Handled in main
        } else if (arg == "--mode" && i + 1 < argc) {
            mode = argv[++i];
            std::transform(mode.begin(), mode.end(), mode.begin(), ::toupper);
        } else if (arg == "--config" && i + 1 < argc) {
            ++i; // Skip value, already handled
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
        } else if (arg == "--save-logs" && i + 1 < argc) {
            std::string val = argv[++i];
            if (val == "true") {
                config.setSaveLogs(true);
            } else if (val == "false") {
                config.setSaveLogs(false);
            } else {
                throw std::runtime_error("Invalid value for --save-logs. Must be 'true' or 'false'.");
            }
        } else if (arg.rfind("--", 0) == 0) {
            const std::vector<std::string> known_args = {"--mode", "--config", "--target", "--port", "--packet-size", "--num-packets", "--interval-ms", "--save-logs", "--help", "-h"};
            bool is_known = false;
            for(const auto& known : known_args) {
                if (arg == known) {
                    is_known = true;
                    break;
                }
            }
            if (!is_known) {
                throw std::runtime_error("Unknown argument: " + arg);
            }
        }
    }

    // Mode is mandatory on the command line
    if (mode == "CLIENT") {
        config.setMode(Config::TestMode::CLIENT);
    } else if (mode == "SERVER") {
        config.setMode(Config::TestMode::SERVER);
    } else {
        throw std::runtime_error("Mode (--mode) must be specified as either 'client' or 'server'.");
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
              << "  --save-logs <true|false>  Save console logs to a file in the 'Log' directory.\n"
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