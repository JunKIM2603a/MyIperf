#include "config.h"
#include <stdexcept> // Required for std::invalid_argument

/**
 * @brief Default constructor.
 * Initializes configuration with default values.
 */
Config::Config() : 
    packetSize(1024),      // Default packet size: 1024 bytes
    numPackets(0),         // 0 means unlimited until manually stopped
    sendIntervalMs(0),     // 0 means no delay between sends
    protocol("TCP"),       // Default protocol: TCP
    targetIP("127.0.0.1"), // Default IP: localhost
    port(5201),            // Default port: 5201
    mode(TestMode::CLIENT) // Default mode: Client
{}

/**
 * @brief Sets the size of network packets.
 * @param size The packet size in bytes.
 * @throws std::invalid_argument if the size is not positive.
 */
void Config::setPacketSize(int size) {
    if (size <= 0) {
        throw std::invalid_argument("Error: Packet size must be a positive integer.");
    }
    packetSize = size;
}

int Config::getPacketSize() const {
    return packetSize;
}

void Config::setNumPackets(int count) {
    if (count < 0) {
        throw std::invalid_argument("Error: numPackets must be >= 0.");
    }
    numPackets = count;
}

int Config::getNumPackets() const {
    return numPackets;
}

void Config::setSendIntervalMs(int intervalMs) {
    if (intervalMs < 0) {
        throw std::invalid_argument("Error: sendIntervalMs must be >= 0.");
    }
    sendIntervalMs = intervalMs;
}

int Config::getSendIntervalMs() const {
    return sendIntervalMs;
}





/**
 * @brief Sets the network protocol.
 * @param proto The protocol name (e.g., "TCP").
 * @throws std::invalid_argument if the protocol is not supported.
 */
void Config::setProtocol(const std::string& proto) {
    // Currently, only TCP is supported. This can be expanded later.
    if (proto != "TCP") {
        throw std::invalid_argument("Error: Unsupported protocol specified. Only 'TCP' is supported.");
    }
    protocol = proto;
}

std::string Config::getProtocol() const {
    return protocol;
}

void Config::setTargetIP(const std::string& ip) {
    targetIP = ip;
}

std::string Config::getTargetIP() const {
    return targetIP;
}

/**
 * @brief Sets the network port.
 * @param p The port number.
 * @throws std::invalid_argument if the port is outside the valid range (1-65535).
 */
void Config::setPort(int p) {
    if (p <= 0 || p > 65535) {
        throw std::invalid_argument("Error: Port number must be between 1 and 65535.");
    }
    port = p;
}

int Config::getPort() const {
    return port;
}

void Config::setMode(TestMode m) {
    mode = m;
}

Config::TestMode Config::getMode() const {
    return mode;
}

/**
 * @brief Converts the Config object to a JSON representation.
 * @return A nlohmann::json object.
 */
nlohmann::json Config::toJson() const {
    nlohmann::json root;
    root["packetSize"] = packetSize;
    root["numPackets"] = numPackets;
    root["sendIntervalMs"] = sendIntervalMs;
    
    root["protocol"] = protocol;
    root["targetIP"] = targetIP;
    root["port"] = port;
    root["mode"] = (mode == TestMode::CLIENT ? "CLIENT" : "SERVER");
    return root;
}

/**
 * @brief Creates a Config object from a JSON representation.
 * @param json The nlohmann::json object to parse.
 * @return A populated Config object.
 * @throws std::invalid_argument if the JSON contains invalid values.
 */
Config Config::fromJson(const nlohmann::json& json) {
    Config config;
    if (json.contains("packetSize")) config.setPacketSize(json["packetSize"].get<int>());
    if (json.contains("numPackets")) config.setNumPackets(json["numPackets"].get<int>());
    if (json.contains("sendIntervalMs")) config.setSendIntervalMs(json["sendIntervalMs"].get<int>());
    
    if (json.contains("protocol")) config.setProtocol(json["protocol"].get<std::string>());
    if (json.contains("targetIP")) config.setTargetIP(json["targetIP"].get<std::string>());
    if (json.contains("port")) config.setPort(json["port"].get<int>());
    if (json.contains("mode")) {
        std::string modeStr = json["mode"].get<std::string>();
        if (modeStr == "CLIENT") {
            config.setMode(TestMode::CLIENT);
        } else if (modeStr == "SERVER") {
            config.setMode(TestMode::SERVER);
        } else {
            throw std::invalid_argument("Error: Invalid mode in configuration file: " + modeStr);
        }
    }
    return config;
}