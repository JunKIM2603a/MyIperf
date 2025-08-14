#pragma once

#include "Config.h"
#include <string>

/**
 * @class ConfigParser
 * @brief Responsible for reading and parsing a JSON configuration file.
 *
 * This class takes a file path to a JSON configuration file, reads the content,
 * parses it, and populates a Config object with the settings found in the file.
 */
class ConfigParser {
public:
    /**
     * @brief Constructs a ConfigParser.
     * @param filepath The path to the JSON configuration file.
     */
    ConfigParser(const std::string& filepath);

    /**
     * @brief Loads and parses the configuration file.
     * @return True if the file was successfully loaded and parsed, false otherwise.
     */
    bool load();

    /**
     * @brief Gets the configuration data that was loaded from the file.
     * @return A Config object containing the parsed settings.
     */
    Config getConfig() const;

private:
    std::string filepath; // The path to the configuration file.
    Config configData;    // The Config object where the parsed data is stored.
};