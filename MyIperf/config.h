#pragma once

#include <string>
#include "nlohmann/json.hpp" // For handling JSON data

/**
 * @brief Converts an enum value to its underlying integral type.
 * @tparam E The enum type.
 * @param e The enum value.
 * @return The underlying integral value of the enum.
 */
template <class E>
constexpr std::underlying_type_t<E> to_underlying(E e) noexcept {
    return static_cast<std::underlying_type_t<E>>(e);
}

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
    enum class TestMode : uint8_t{ 
        CLIENT, /**< The application will act as a client, initiating the connection. */
        SERVER  /**< The application will act as a server, listening for a connection. */
    };

    /**
     * @brief Constructs a Config object with default values.
     */
    Config();
    
    // --- Setters and Getters for configuration properties ---

    /**
     * @brief Sets the size of each data packet.
     * @param size The packet size in bytes.
     */
    void setPacketSize(int size);
    /**
     * @brief Gets the size of each data packet.
     * @return The packet size in bytes.
     */
    int getPacketSize() const;

    /**
     * @brief Sets the number of packets to send.
     * @param count The number of packets. 0 means unlimited.
     */
    void setNumPackets(int count);
    /**
     * @brief Gets the number of packets to send.
     * @return The number of packets.
     */
    int getNumPackets() const;

    /**
     * @brief Sets the interval between sending packets.
     * @param intervalMs The interval in milliseconds. 0 means no delay.
     */
    void setSendIntervalMs(int intervalMs);
    /**
     * @brief Gets the interval between sending packets.
     * @return The interval in milliseconds.
     */
    int getSendIntervalMs() const;

    /**
     * @brief Sets the network protocol.
     * @param proto The protocol string (e.g., "TCP").
     */
    void setProtocol(const std::string& proto);
    /**
     * @brief Gets the network protocol.
     * @return The protocol string.
     */
    std::string getProtocol() const;

    /**
     * @brief Sets the target IP address.
     * @param ip The IP address.
     */
    void setTargetIP(const std::string& ip);
    /**
     * @brief Gets the target IP address.
     * @return The IP address.
     */
    std::string getTargetIP() const;

    /**
     * @brief Sets the port number.
     * @param p The port number.
     */
    void setPort(int p);
    /**
     * @brief Gets the port number.
     * @return The port number.
     */
    int getPort() const;

    /**
     * @brief Sets the operational mode.
     * @param m The test mode (CLIENT or SERVER).
     */
    void setMode(TestMode m);
    /**
     * @brief Gets the operational mode.
     * @return The test mode.
     */
    TestMode getMode() const;

    /**
     * @brief Sets whether to save logs to a file.
     * @param save True to save logs, false otherwise.
     */
    void setSaveLogs(bool save);
    /**
     * @brief Gets whether to save logs to a file.
     * @return True if logs are to be saved, false otherwise.
     */
    bool getSaveLogs() const;

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
    /**< The size of each data packet in bytes. */
    int packetSize;
    /**< Number of packets to send during the test (client-side). 0 means unlimited. */
    int numPackets;
    /**< Optional interval between sends in milliseconds. 0 means no delay. */
    int sendIntervalMs;
    /**< The network protocol to be used (e.g., "TCP"). */
    std::string protocol;
    /**< The IP address for the client to connect to or the server to listen on. */
    std::string targetIP;
    /**< The port number for the network connection. */
    int port;
    /**< The operational mode: CLIENT or SERVER. */
    TestMode mode;
    /**< Whether to save logs to a file. */
    bool saveLogs;
};