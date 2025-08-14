#include "ConfigParser.h"
#include "Logger.h"
#include <fstream>      // For file input operations
#include <stdexcept>    // For standard exceptions

/**
 * @brief Constructs the ConfigParser.
 * @param filepath The path to the JSON configuration file.
 */
ConfigParser::ConfigParser(const std::string& filepath) : filepath(filepath) {}

/**
 * @brief Loads and parses the JSON configuration file.
 * @return True on success, false on failure.
 */
bool ConfigParser::load() {
    std::ifstream ifs(filepath);
    if (!ifs.is_open()) {
        Logger::log("Error: Could not open configuration file at " + filepath);
        return false;
    }

    try {
        // Parse the JSON data directly from the input file stream.
        nlohmann::json root = nlohmann::json::parse(ifs);
        // Use the static fromJson method of the Config class to populate the data.
        configData = Config::fromJson(root);
        Logger::log("Info: Configuration loaded successfully from " + filepath);
        return true;
    } catch (const nlohmann::json::parse_error& e) {
        // Handle errors related to invalid JSON format.
        Logger::log("Error: Failed to parse JSON in configuration file. Details: " + std::string(e.what()));
        return false;
    } catch (const std::exception& e) {
        // Handle other exceptions that might occur during parsing (e.g., invalid values).
        Logger::log("Error: An exception occurred while loading the configuration. Details: " + std::string(e.what()));
        return false;
    }
}

/**
 * @brief Returns the loaded configuration data.
 * @return The Config object.
 */
Config ConfigParser::getConfig() const {
    return configData;
}