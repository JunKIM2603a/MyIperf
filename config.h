#pragma once

#include <string>
#include "nlohmann/json.hpp" // For handling JSON data

/**
 * @class Config
 * @brief Holds all configuration settings for the network test.
 *
 * This class encapsulates all the parameters that define how a network test
 * will be conducted, including packet size, test duration, protocol, and
 * connection details. It also provides methods for serialization to and from JSON.
 */
class Config {
public:
    /**
     * @enum TestMode
     * @brief Defines the operational modes for the application.
     */
    enum class TestMode { 
        CLIENT, // The application will act as a client, initiating the connection.
        SERVER  // The application will act as a server, listening for a connection.
    };

    // Constructor: Initializes the configuration with default values.
    Config();
    
    // --- Setters and Getters for configuration properties ---

    void setPacketSize(int size);
    int getPacketSize() const;

    void setNumPackets(int count);
    int getNumPackets() const;

    void setSendIntervalMs(int intervalMs);
    int getSendIntervalMs() const;

    void setProtocol(const std::string& proto);
    std::string getProtocol() const;

    void setTargetIP(const std::string& ip);
    std::string getTargetIP() const;

    void setPort(int p);
    int getPort() const;

    void setMode(TestMode m);
    TestMode getMode() const;

    /**
     * @brief Serializes the Config object to a JSON object.
     * @return A nlohmann::json object representing the configuration.
     */
    nlohmann::json toJson() const;

    /**
     * @brief Deserializes a JSON object to a Config object.
     * @param json The nlohmann::json object to parse.
     * @return A new Config object with settings from the JSON data.
     */
    static Config fromJson(const nlohmann::json& json);

private:
    int packetSize;      // The size of each data packet in bytes.
    int numPackets;      // Number of packets to send during the test (client-side). 0 means unlimited.
    int sendIntervalMs;  // Optional interval between sends in milliseconds. 0 means no delay.
    std::string protocol; // The network protocol to be used (e.g., "TCP").
    std::string targetIP; // The IP address for the client to connect to or the server to listen on.
    int port;            // The port number for the network connection.
    TestMode mode;       // The operational mode: CLIENT or SERVER.
};