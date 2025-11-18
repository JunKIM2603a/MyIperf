#include "ControlClient.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <thread>
#include <chrono>

namespace TestRunner2 {

ControlClient::ControlClient(const std::string& serverIP,
                            int controlPort,
                            const std::string& ipeftcPath)
    : m_serverIP(serverIP),
      m_controlPort(controlPort),
      m_ipeftcPath(ipeftcPath),
      m_wsaInitialized(false) {
}

ControlClient::~ControlClient() {
    if (m_wsaInitialized) {
        WSACleanup();
    }
}

bool ControlClient::InitializeWinsock() {
    if (m_wsaInitialized) {
        return true;
    }

    WSADATA wsaData;
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0) {
        std::cerr << "[ControlClient] WSAStartup failed: " << result << std::endl;
        return false;
    }
    
    m_wsaInitialized = true;
    std::cout << "[ControlClient] Winsock initialized" << std::endl;
    return true;
}

SOCKET ControlClient::ConnectToServer() {
    SOCKET clientSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (clientSocket == INVALID_SOCKET) {
        std::cerr << "[ControlClient] Socket creation failed: " << WSAGetLastError() << std::endl;
        return INVALID_SOCKET;
    }

    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(static_cast<unsigned short>(m_controlPort));
    
    if (inet_pton(AF_INET, m_serverIP.c_str(), &serverAddr.sin_addr) <= 0) {
        std::cerr << "[ControlClient] Invalid server IP address: " << m_serverIP << std::endl;
        closesocket(clientSocket);
        return INVALID_SOCKET;
    }

    std::cout << "[ControlClient] Connecting to " << m_serverIP << ":" << m_controlPort << "..." << std::endl;

    if (connect(clientSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        std::cerr << "[ControlClient] Connection failed: " << WSAGetLastError() << std::endl;
        closesocket(clientSocket);
        return INVALID_SOCKET;
    }

    std::cout << "[ControlClient] Connected to server" << std::endl;
    return clientSocket;
}

void ControlClient::Disconnect(SOCKET socket) {
    if (socket != INVALID_SOCKET) {
        closesocket(socket);
    }
}

bool ControlClient::SendMessage(SOCKET socket, const std::string& message) {
    // Send message length first (4 bytes)
    uint32_t messageLength = static_cast<uint32_t>(message.size());
    uint32_t networkLength = htonl(messageLength);
    
    int sent = send(socket, (char*)&networkLength, sizeof(networkLength), 0);
    if (sent != sizeof(networkLength)) {
        std::cerr << "[ControlClient] Failed to send message length" << std::endl;
        return false;
    }

    // Send message data
    sent = send(socket, message.c_str(), messageLength, 0);
    if (static_cast<long long>(sent) != static_cast<long long>(messageLength)) {
        std::cerr << "[ControlClient] Failed to send message data" << std::endl;
        return false;
    }

    return true;
}

bool ControlClient::ReceiveMessage(SOCKET socket, std::string& message, int timeoutMs) {
    // Set receive timeout
    DWORD timeout = timeoutMs;
    setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));

    // Receive message length first (4 bytes)
    uint32_t networkLength;
    int received = recv(socket, (char*)&networkLength, sizeof(networkLength), 0);
    if (received != sizeof(networkLength)) {
        std::cerr << "[ControlClient] Failed to receive message length" << std::endl;
        return false;
    }

    uint32_t messageLength = ntohl(networkLength);
    if (messageLength == 0 || messageLength > Protocol::MAX_MESSAGE_SIZE) {
        std::cerr << "[ControlClient] Invalid message length: " << messageLength << std::endl;
        return false;
    }

    // Receive message data
    std::vector<char> buffer(messageLength + 1);
    unsigned int totalReceived = 0;
    while (totalReceived < messageLength) {
        received = recv(socket, buffer.data() + totalReceived, messageLength - totalReceived, 0);
        if (received <= 0) {
            std::cerr << "[ControlClient] Failed to receive message data" << std::endl;
            return false;
        }
        totalReceived += received;
    }
    
    buffer[messageLength] = '\0';
    message = std::string(buffer.data(), messageLength);
    return true;
}

PortTestResult ControlClient::ExecutePortTest(const TestConfig& config) {
    PortTestResult result;
    result.port = config.port;
    result.success = false;

    try {
        // Connect to control server
        SOCKET controlSocket = ConnectToServer();
        if (controlSocket == INVALID_SOCKET) {
            result.errorMessage = "Failed to connect to control server";
            return result;
        }

        // Send CONFIG_REQUEST
        ConfigRequestMessage configMsg;
        configMsg.config = config;
        std::string configJson = SerializeConfigRequest(configMsg);
        
        std::cout << "[ControlClient] Sending CONFIG_REQUEST for port " << config.port << std::endl;
        if (!SendMessage(controlSocket, configJson)) {
            result.errorMessage = "Failed to send CONFIG_REQUEST";
            Disconnect(controlSocket);
            return result;
        }

        // Wait for SERVER_READY
        std::string responseMsg;
        if (!ReceiveMessage(controlSocket, responseMsg, Protocol::SERVER_START_TIMEOUT_MS)) {
            result.errorMessage = "Timeout waiting for SERVER_READY";
            Disconnect(controlSocket);
            return result;
        }

        MessageType responseType = GetMessageType(responseMsg);
        if (responseType == MessageType::ERROR_MESSAGE) {
            ErrorMessage errMsg = DeserializeError(responseMsg);
            result.errorMessage = "Server error: " + errMsg.error;
            Disconnect(controlSocket);
            return result;
        }

        if (responseType != MessageType::SERVER_READY) {
            result.errorMessage = "Expected SERVER_READY, got " + MessageTypeToString(responseType);
            Disconnect(controlSocket);
            return result;
        }

        ServerReadyMessage readyMsg = DeserializeServerReady(responseMsg);
        std::cout << "[ControlClient] Server ready on port " << readyMsg.port << std::endl;

        // Launch IPEFTC client
        ProcessHandles clientProcess;
        if (!m_processManager.LaunchIPEFTCClient(m_ipeftcPath, m_serverIP, config, clientProcess)) {
            result.errorMessage = "Failed to launch IPEFTC client";
            Disconnect(controlSocket);
            return result;
        }

        // Wait for IPEFTC client to complete and capture output
        std::cout << "[ControlClient] Waiting for IPEFTC client to complete..." << std::endl;
        std::string clientOutput = m_processManager.CaptureProcessOutput(clientProcess);
        m_processManager.CloseHandles(clientProcess);

        // Parse client results
        result.clientResult = m_processManager.ParseTestSummary(clientOutput, "Client", config.port);
        std::cout << "[ControlClient] Client test completed" << std::endl;

        // Request results from server
        ResultsRequestMessage resultsReq;
        resultsReq.port = config.port;
        resultsReq.clientResult = result.clientResult;  // Include client's result
        
        std::cout << "[ControlClient] Requesting results from server..." << std::endl;
        if (!SendMessage(controlSocket, SerializeResultsRequest(resultsReq))) {
            result.errorMessage = "Failed to send RESULTS_REQUEST";
            Disconnect(controlSocket);
            return result;
        }

        // Wait for RESULTS_RESPONSE
        if (!ReceiveMessage(controlSocket, responseMsg, 60000)) {
            result.errorMessage = "Timeout waiting for RESULTS_RESPONSE";
            Disconnect(controlSocket);
            return result;
        }

        responseType = GetMessageType(responseMsg);
        if (responseType == MessageType::ERROR_MESSAGE) {
            ErrorMessage errMsg = DeserializeError(responseMsg);
            result.errorMessage = "Server error: " + errMsg.error;
            Disconnect(controlSocket);
            return result;
        }

        if (responseType != MessageType::RESULTS_RESPONSE) {
            result.errorMessage = "Expected RESULTS_RESPONSE, got " + MessageTypeToString(responseType);
            Disconnect(controlSocket);
            return result;
        }

        ResultsResponseMessage resultsMsg = DeserializeResultsResponse(responseMsg);
        result.serverResult = resultsMsg.serverResult;
        
        std::cout << "[ControlClient] Received server results" << std::endl;

        // Determine overall success
        result.success = result.clientResult.success && result.serverResult.success;

        Disconnect(controlSocket);

    } catch (const std::exception& e) {
        result.errorMessage = std::string("Exception: ") + e.what();
        std::cerr << "[ControlClient] Exception in ExecutePortTest: " << e.what() << std::endl;
    }

    return result;
}

PortTestResult ControlClient::RunSinglePortTest(const TestConfig& config) {
    if (!InitializeWinsock()) {
        PortTestResult result;
        result.port = config.port;
        result.success = false;
        result.errorMessage = "Failed to initialize Winsock";
        return result;
    }

    std::cout << "\n==================================================" << std::endl;
    std::cout << "Starting Single Port Test" << std::endl;
    std::cout << "Port: " << config.port << std::endl;
    std::cout << "Packet Size: " << config.packetSize << " bytes" << std::endl;
    std::cout << "Num Packets: " << config.numPackets << std::endl;
    std::cout << "Interval: " << config.sendIntervalMs << " ms" << std::endl;
    std::cout << "==================================================" << std::endl;

    return ExecutePortTest(config);
}

std::vector<PortTestResult> ControlClient::RunMultiPortTest(const TestConfig& baseConfig, int numPorts) {
    if (!InitializeWinsock()) {
        std::vector<PortTestResult> results(numPorts);
        for (int i = 0; i < numPorts; i++) {
            results[i].port = baseConfig.port + i;
            results[i].success = false;
            results[i].errorMessage = "Failed to initialize Winsock";
        }
        return results;
    }

    std::cout << "\n==================================================" << std::endl;
    std::cout << "Starting Multi-Port Test" << std::endl;
    std::cout << "Number of Ports: " << numPorts << std::endl;
    std::cout << "Starting Port: " << baseConfig.port << std::endl;
    std::cout << "Packet Size: " << baseConfig.packetSize << " bytes" << std::endl;
    std::cout << "Num Packets: " << baseConfig.numPackets << std::endl;
    std::cout << "Interval: " << baseConfig.sendIntervalMs << " ms" << std::endl;
    std::cout << "==================================================" << std::endl;

    std::vector<PortTestResult> results(numPorts);
    std::vector<std::thread> threads;

    // Launch threads for each port
    for (int i = 0; i < numPorts; i++) {
        threads.emplace_back([this, &results, baseConfig, i]() {
            TestConfig config = baseConfig;
            config.port = baseConfig.port + i;
            results[i] = ExecutePortTest(config);
        });
    }

    // Wait for all threads to complete
    for (auto& thread : threads) {
        thread.join();
    }

    std::cout << "\n[ControlClient] All port tests completed" << std::endl;
    return results;
}

void ControlClient::PrintResults(const std::vector<PortTestResult>& results,
                                 long long expectedPackets,
                                 long long expectedBytes) {
    std::cout << "\n--- FINAL TEST SUMMARY ---" << std::endl;
    std::cout << std::left << std::setw(8) << "Role"
              << std::setw(8) << "Port"
              << std::setw(15) << "Duration (s)"
              << std::setw(18) << "Throughput (Mbps)"
              << std::setw(22) << "Total Bytes Rx"
              << std::setw(24) << "Total Packets Rx"
              << std::setw(10) << "Status" << std::endl;
    std::cout << std::string(104, '-') << std::endl;

    bool allOk = true;
    int totalTests = 0;
    int passedTests = 0;

    for (const auto& portResult : results) {
        if (!portResult.success) {
            std::cout << "Port " << portResult.port << " FAILED: " << portResult.errorMessage << std::endl;
            allOk = false;
            totalTests += 2;
            continue;
        }

        // Server results
        {
            const auto& res = portResult.serverResult;
            bool packetsMatch = (res.totalPackets == expectedPackets);
            bool bytesMatch = (res.totalBytes == expectedBytes);
            bool noErrors = (res.sequenceErrors == 0 && res.checksumErrors == 0 && res.contentMismatches == 0);
            bool pass = res.success && packetsMatch && bytesMatch && noErrors;

            std::cout << std::left << std::setw(8) << res.role
                      << std::setw(8) << res.port
                      << std::setw(15) << std::fixed << std::setprecision(2) << res.duration
                      << std::setw(18) << res.throughput
                      << std::setw(22) << res.totalBytes
                      << std::setw(24) << res.totalPackets
                      << std::setw(10) << (pass ? "PASS" : "FAIL") << std::endl;

            if (!pass) {
                allOk = false;
                if (!packetsMatch || !bytesMatch || !noErrors) {
                    std::cout << "  -> Expected " << expectedPackets << " packets (" << expectedBytes << " bytes)";
                    if (!noErrors) {
                        std::cout << ", Errors detected";
                    }
                    std::cout << std::endl;
                }
            } else {
                passedTests++;
            }
            totalTests++;
        }

        // Client results
        {
            const auto& res = portResult.clientResult;
            bool packetsMatch = (res.totalPackets == expectedPackets);
            bool bytesMatch = (res.totalBytes == expectedBytes);
            bool noErrors = (res.sequenceErrors == 0 && res.checksumErrors == 0 && res.contentMismatches == 0);
            bool pass = res.success && packetsMatch && bytesMatch && noErrors;

            std::cout << std::left << std::setw(8) << res.role
                      << std::setw(8) << res.port
                      << std::setw(15) << std::fixed << std::setprecision(2) << res.duration
                      << std::setw(18) << res.throughput
                      << std::setw(22) << res.totalBytes
                      << std::setw(24) << res.totalPackets
                      << std::setw(10) << (pass ? "PASS" : "FAIL") << std::endl;

            if (!pass) {
                allOk = false;
                if (!packetsMatch || !bytesMatch || !noErrors) {
                    std::cout << "  -> Expected " << expectedPackets << " packets (" << expectedBytes << " bytes)";
                    if (!noErrors) {
                        std::cout << ", Errors detected";
                    }
                    std::cout << std::endl;
                }
            } else {
                passedTests++;
            }
            totalTests++;
        }
    }

    std::cout << "\n--- Summary ---" << std::endl;
    std::cout << "Total Tests: " << totalTests << std::endl;
    std::cout << "Passed: " << passedTests << std::endl;
    std::cout << "Failed: " << (totalTests - passedTests) << std::endl;

    if (allOk) {
        std::cout << "\nSUCCESS: All tests passed!" << std::endl;
    } else {
        std::cout << "\nWARNING: Some tests failed." << std::endl;
    }
}

} // namespace TestRunner2

