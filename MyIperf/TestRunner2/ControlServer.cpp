#include "ControlServer.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <thread>

namespace TestRunner2 {

ControlServer::ControlServer(int controlPort, const std::string& ipeftcPath, bool defaultSaveLogs)
    : m_controlPort(controlPort),
      m_ipeftcPath(ipeftcPath),
      m_defaultSaveLogs(defaultSaveLogs),
      m_serverSocket(INVALID_SOCKET),
      m_running(false) {
}

ControlServer::~ControlServer() {
    Stop();
}

bool ControlServer::InitializeWinsock() {
    WSADATA wsaData;
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0) {
        std::cerr << "[ControlServer] WSAStartup failed: " << result << std::endl;
        return false;
    }
    std::cout << "[ControlServer] Winsock initialized" << std::endl;
    return true;
}

bool ControlServer::CreateServerSocket() {
    // Create socket
    m_serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (m_serverSocket == INVALID_SOCKET) {
        std::cerr << "[ControlServer] Socket creation failed: " << WSAGetLastError() << std::endl;
        return false;
    }

    // Set socket options
    int opt = 1;
    setsockopt(m_serverSocket, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));

    // Bind socket
    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(static_cast<unsigned short>(m_controlPort));

    if (bind(m_serverSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        std::cerr << "[ControlServer] Bind failed: " << WSAGetLastError() << std::endl;
        closesocket(m_serverSocket);
        m_serverSocket = INVALID_SOCKET;
        return false;
    }

    // Listen
    if (listen(m_serverSocket, SOMAXCONN) == SOCKET_ERROR) {
        std::cerr << "[ControlServer] Listen failed: " << WSAGetLastError() << std::endl;
        closesocket(m_serverSocket);
        m_serverSocket = INVALID_SOCKET;
        return false;
    }

    std::cout << "[ControlServer] Server listening on port " << m_controlPort << std::endl;
    return true;
}

bool ControlServer::Start() {
    if (m_running) {
        std::cerr << "[ControlServer] Server already running" << std::endl;
        return false;
    }

    if (!InitializeWinsock()) {
        return false;
    }

    if (!CreateServerSocket()) {
        WSACleanup();
        return false;
    }

    m_running = true;
    std::cout << "[ControlServer] Server started successfully" << std::endl;

    AcceptConnections();

    return true;
}

void ControlServer::Stop() {
    if (!m_running) {
        return;
    }

    std::cout << "[ControlServer] Stopping server..." << std::endl;
    m_running = false;

    if (m_serverSocket != INVALID_SOCKET) {
        closesocket(m_serverSocket);
        m_serverSocket = INVALID_SOCKET;
    }

    // Wait for all session threads to complete
    {
        std::lock_guard<std::mutex> lock(m_threadsMutex);
        for (auto& thread : m_sessionThreads) {
            if (thread.joinable()) {
                thread.join();
            }
        }
        m_sessionThreads.clear();
    }

    WSACleanup();
    std::cout << "[ControlServer] Server stopped" << std::endl;
}

void ControlServer::AcceptConnections() {
    std::cout << "[ControlServer] Waiting for client connections..." << std::endl;

    while (m_running) {
        sockaddr_in clientAddr;
        int clientAddrSize = sizeof(clientAddr);
        SOCKET clientSocket = accept(m_serverSocket, (sockaddr*)&clientAddr, &clientAddrSize);

        if (clientSocket == INVALID_SOCKET) {
            if (m_running) {
                std::cerr << "[ControlServer] Accept failed: " << WSAGetLastError() << std::endl;
            }
            break;
        }

        char clientIP[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &clientAddr.sin_addr, clientIP, INET_ADDRSTRLEN);
        std::cout << "[ControlServer] Client connected from " << clientIP 
                  << ":" << ntohs(clientAddr.sin_port) << std::endl;

        // Create a new thread to handle this client session
        std::thread sessionThread(&ControlServer::HandleSession, this, clientSocket);
        
        {
            std::lock_guard<std::mutex> lock(m_threadsMutex);
            m_sessionThreads.push_back(std::move(sessionThread));
        }
    }
}

void ControlServer::HandleSession(SOCKET clientSocket) {
    Session session;
    session.clientSocket = clientSocket;
    session.state = SessionState::IDLE;

    std::cout << "[ControlServer] Session started" << std::endl;

    try {
        while (m_running && session.clientSocket != INVALID_SOCKET) {
            std::string receivedMessage;
            
            if (!ReceiveMessage(session.clientSocket, receivedMessage)) {
                std::cout << "[ControlServer] Client disconnected or receive error" << std::endl;
                break;
            }

            // Parse message type
            MessageType msgType;
            try {
                msgType = GetMessageType(receivedMessage);
            } catch (const std::exception& e) {
                std::cerr << "[ControlServer] Failed to parse message: " << e.what() << std::endl;
                ErrorMessage errMsg("Invalid message format");
                SendMessage(session.clientSocket, SerializeError(errMsg));
                continue;
            }

            // Handle message based on type
            switch (msgType) {
                case MessageType::CONFIG_REQUEST:
                    if (!ProcessConfigRequest(session, receivedMessage)) {
                        std::cerr << "[ControlServer] Failed to process CONFIG_REQUEST" << std::endl;
                        goto session_end;
                    }
                    break;

                case MessageType::RESULTS_REQUEST:
                    if (!ProcessResultsRequest(session, receivedMessage)) {
                        std::cerr << "[ControlServer] Failed to process RESULTS_REQUEST" << std::endl;
                        goto session_end;
                    }
                    // After sending results, session is complete
                    goto session_end;

                case MessageType::HEARTBEAT:
                    // Respond to heartbeat
                    SendMessage(session.clientSocket, "{\"messageType\":\"HEARTBEAT\"}");
                    break;

                default:
                    std::cerr << "[ControlServer] Unexpected message type: " 
                              << MessageTypeToString(msgType) << std::endl;
                    ErrorMessage errMsg("Unexpected message type");
                    SendMessage(session.clientSocket, SerializeError(errMsg));
                    break;
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "[ControlServer] Session exception: " << e.what() << std::endl;
    }

session_end:
    // Cleanup
    if (session.ipeftcProcess.processInfo.hProcess) {
        if (m_processManager.IsProcessRunning(session.ipeftcProcess)) {
            m_processManager.TerminateProcess(session.ipeftcProcess);
        }
        m_processManager.CloseHandles(session.ipeftcProcess);
    }

    if (session.clientSocket != INVALID_SOCKET) {
        closesocket(session.clientSocket);
    }

    std::cout << "[ControlServer] Session ended" << std::endl;
}

bool ControlServer::SendMessage(SOCKET socket, const std::string& message) {
    // Send message length first (4 bytes)
    uint32_t messageLength = static_cast<uint32_t>(message.size());
    uint32_t networkLength = htonl(messageLength);
    
    int sent = send(socket, (char*)&networkLength, sizeof(networkLength), 0);
    if (sent != sizeof(networkLength)) {
        std::cerr << "[ControlServer] Failed to send message length" << std::endl;
        return false;
    }

    // Send message data
    sent = send(socket, message.c_str(), messageLength, 0);
    if (static_cast<long long>(sent) != static_cast<long long>(messageLength)) {
        std::cerr << "[ControlServer] Failed to send message data" << std::endl;
        return false;
    }

    return true;
}

bool ControlServer::ReceiveMessage(SOCKET socket, std::string& message, int timeoutMs) {
    // Set receive timeout
    DWORD timeout = timeoutMs;
    setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));

    // Receive message length first (4 bytes)
    uint32_t networkLength;
    int received = recv(socket, (char*)&networkLength, sizeof(networkLength), 0);
    if (received != sizeof(networkLength)) {
        return false;
    }

    uint32_t messageLength = ntohl(networkLength);
    if (messageLength == 0 || messageLength > Protocol::MAX_MESSAGE_SIZE) {
        std::cerr << "[ControlServer] Invalid message length: " << messageLength << std::endl;
        return false;
    }

    // Receive message data
    std::vector<char> buffer(messageLength + 1);
    unsigned int totalReceived = 0;
    while (totalReceived < messageLength) {
        received = recv(socket, buffer.data() + totalReceived, messageLength - totalReceived, 0);
        if (received <= 0) {
            std::cerr << "[ControlServer] Failed to receive message data" << std::endl;
            return false;
        }
        totalReceived += received;
    }
    
    buffer[messageLength] = '\0';
    message = std::string(buffer.data(), messageLength);
    return true;
}

bool ControlServer::ProcessConfigRequest(Session& session, const std::string& message) {
    std::cout << "[ControlServer] Processing CONFIG_REQUEST" << std::endl;

    try {
        ConfigRequestMessage configMsg = DeserializeConfigRequest(message);
        session.config = configMsg.config;
        
        // Apply server's default saveLogs setting
        session.config.saveLogs = m_defaultSaveLogs;
        
        session.state = SessionState::CONFIG_RECEIVED;

        std::cout << "[ControlServer] Test config received - Port: " << session.config.port 
                  << ", Packets: " << session.config.numPackets 
                  << ", Size: " << session.config.packetSize
                  << ", SaveLogs: " << (session.config.saveLogs ? "true" : "false") << std::endl;

        // Launch IPEFTC server
        session.state = SessionState::SERVER_STARTING;
        if (!m_processManager.LaunchIPEFTCServer(m_ipeftcPath, session.config, session.ipeftcProcess)) {
            std::cerr << "[ControlServer] Failed to launch IPEFTC server" << std::endl;
            ErrorMessage errMsg("Failed to launch IPEFTC server");
            SendMessage(session.clientSocket, SerializeError(errMsg));
            session.state = SessionState::ERROR_STATE;
            return false;
        }

        // Wait for server to be ready
        if (!m_processManager.WaitForServerReady(session.ipeftcProcess, 
                                                 session.processOutput,
                                                 Protocol::SERVER_START_TIMEOUT_MS)) {
            std::cerr << "[ControlServer] IPEFTC server failed to start" << std::endl;
            ErrorMessage errMsg("IPEFTC server failed to start");
            SendMessage(session.clientSocket, SerializeError(errMsg));
            session.state = SessionState::ERROR_STATE;
            return false;
        }

        session.state = SessionState::SERVER_READY;

        // Send SERVER_READY message
        ServerReadyMessage readyMsg;
        readyMsg.port = session.config.port;
        readyMsg.serverIP = "0.0.0.0";  // Client should use the connection IP
        
        if (!SendMessage(session.clientSocket, SerializeServerReady(readyMsg))) {
            std::cerr << "[ControlServer] Failed to send SERVER_READY" << std::endl;
            return false;
        }

        std::cout << "[ControlServer] SERVER_READY sent" << std::endl;

        // Start waiting for test completion in background
        session.state = SessionState::TESTING;
        
        return true;

    } catch (const std::exception& e) {
        std::cerr << "[ControlServer] Exception in ProcessConfigRequest: " << e.what() << std::endl;
        ErrorMessage errMsg(std::string("Configuration error: ") + e.what());
        SendMessage(session.clientSocket, SerializeError(errMsg));
        return false;
    }
}

bool ControlServer::ProcessResultsRequest(Session& session, const std::string& message) {
    std::cout << "[ControlServer] Processing RESULTS_REQUEST" << std::endl;
    
    // Deserialize to get client result
    ResultsRequestMessage resultsReq;
    try {
        resultsReq = DeserializeResultsRequest(message);
        std::cout << "[ControlServer] Received client result: " 
                  << resultsReq.clientResult.role << " Port " << resultsReq.clientResult.port << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "[ControlServer] Failed to parse RESULTS_REQUEST: " << e.what() << std::endl;
    }
    
    // Wait for IPEFTC process to complete if still running
    if (m_processManager.IsProcessRunning(session.ipeftcProcess)) {
        std::cout << "[ControlServer] Waiting for IPEFTC server to complete..." << std::endl;
        
        // Continue reading output while process runs
        const std::string completionMsg = "IPEFTC application finished";
        bool finished = false;
        auto startTime = std::chrono::steady_clock::now();
        
        // Calculate dynamic timeout
        double timeoutSec = 60.0;
        if (session.config.sendIntervalMs > 0 && session.config.numPackets > 0) {
            double estimatedPhaseSec = static_cast<double>(session.config.numPackets) * 
                                      static_cast<double>(session.config.sendIntervalMs) / 1000.0;
            double estimatedTotalSec = (estimatedPhaseSec * 2.0) + 15.0;
            timeoutSec = std::max(timeoutSec, std::min(estimatedTotalSec, 600.0));
        }
        const auto timeout = std::chrono::seconds(static_cast<long long>(timeoutSec));
        
        while (!finished && (std::chrono::steady_clock::now() - startTime < timeout)) {
            std::string newOutput = m_processManager.ReadAvailableOutput(session.ipeftcProcess);
            session.processOutput += newOutput;
            
            if (session.processOutput.find(completionMsg) != std::string::npos) {
                finished = true;
                break;
            }
            
            if (!m_processManager.IsProcessRunning(session.ipeftcProcess)) {
                finished = true;
                break;
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        // Read any remaining output
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        session.processOutput += m_processManager.ReadAvailableOutput(session.ipeftcProcess);
        
        if (!finished) {
            std::cerr << "[ControlServer] IPEFTC server timed out" << std::endl;
            m_processManager.TerminateProcess(session.ipeftcProcess);
        }
    }

    // Parse results
    session.result = m_processManager.ParseTestSummary(session.processOutput, "Server", session.config.port);
    session.state = SessionState::TEST_COMPLETE;

    // Save both server and client results to accumulated results
    long long expectedBytes = session.config.packetSize * session.config.numPackets;
    {
        std::lock_guard<std::mutex> lock(m_resultsMutex);
        session.result.expectedPackets = session.config.numPackets;
        session.result.expectedBytes = expectedBytes;
        m_allResults.push_back(session.result);
        
        // Also save client result if available
        if (resultsReq.clientResult.success) {
            resultsReq.clientResult.expectedPackets = session.config.numPackets;
            resultsReq.clientResult.expectedBytes = expectedBytes;
            m_allResults.push_back(resultsReq.clientResult);
        }
    }

    // Print individual results (server only)
    PrintServerResult(session.result, session.config.numPackets, expectedBytes);
    
    // Print accumulated results (includes both server and client)
    PrintAccumulatedResults();

    // Send results
    ResultsResponseMessage resultsMsg;
    resultsMsg.serverResult = session.result;
    
    if (!SendMessage(session.clientSocket, SerializeResultsResponse(resultsMsg))) {
        std::cerr << "[ControlServer] Failed to send RESULTS_RESPONSE" << std::endl;
        return false;
    }

    std::cout << "[ControlServer] Results sent to client - Success: " << session.result.success 
              << ", Throughput: " << session.result.throughput << " Mbps" << std::endl;

    session.state = SessionState::RESULTS_SENT;
    return true;
}

void ControlServer::PrintServerResult(const TestResult& result, 
                                      long long expectedPackets, 
                                      long long expectedBytes) {
    std::cout << "\n==================================================" << std::endl;
    std::cout << "--- SERVER SIDE TEST SUMMARY ---" << std::endl;
    std::cout << "==================================================" << std::endl;
    
    // Validation
    bool packetsMatch = (result.totalPackets == expectedPackets);
    bool bytesMatch = (result.totalBytes == expectedBytes);
    bool noErrors = (result.sequenceErrors == 0 && 
                     result.checksumErrors == 0 && 
                     result.contentMismatches == 0);
    bool pass = result.success && packetsMatch && bytesMatch && noErrors;
    
    // Print in table format similar to client
    std::cout << std::left << std::setw(8) << "Role"
              << std::setw(8) << "Port"
              << std::setw(15) << "Duration (s)"
              << std::setw(18) << "Throughput (Mbps)"
              << std::setw(22) << "Total Bytes Rx"
              << std::setw(24) << "Total Packets Rx"
              << std::setw(10) << "Status" << std::endl;
    std::cout << std::string(104, '-') << std::endl;
    
    std::cout << std::left << std::setw(8) << result.role
              << std::setw(8) << result.port
              << std::setw(15) << std::fixed << std::setprecision(2) << result.duration
              << std::setw(18) << result.throughput
              << std::setw(22) << result.totalBytes
              << std::setw(24) << result.totalPackets
              << std::setw(10) << (pass ? "PASS" : "FAIL") << std::endl;
    
    if (!pass) {
        std::cout << "  -> Expected: " << expectedPackets << " packets (" 
                  << expectedBytes << " bytes)";
        if (!noErrors) {
            std::cout << ", Errors detected";
        }
        std::cout << std::endl;
    }
    
    // Print detailed summary
    std::cout << "\n--- Summary ---" << std::endl;
    std::cout << "Test Result: " << (pass ? "PASS" : "FAIL") << std::endl;
    
    if (!noErrors) {
        std::cout << "Sequence Errors: " << result.sequenceErrors << std::endl;
        std::cout << "Checksum Errors: " << result.checksumErrors << std::endl;
        std::cout << "Content Mismatches: " << result.contentMismatches << std::endl;
    }
    
    if (!result.failureReason.empty()) {
        std::cout << "Failure Reason: " << result.failureReason << std::endl;
    }
    
    std::cout << "==================================================" << std::endl;
}

void ControlServer::PrintAccumulatedResults() {
    std::lock_guard<std::mutex> lock(m_resultsMutex);
    
    if (m_allResults.empty()) {
        return;
    }

    std::cout << "\n==================================================" << std::endl;
    std::cout << "=== ACCUMULATED RESULTS (" << m_allResults.size() << " sessions) ===" << std::endl;
    std::cout << "==================================================" << std::endl;

    std::cout << std::left << std::setw(8) << "Role"
              << std::setw(8) << "Port"
              << std::setw(15) << "Duration (s)"
              << std::setw(18) << "Throughput (Mbps)"
              << std::setw(22) << "Total Bytes Rx"
              << std::setw(24) << "Total Packets Rx"
              << std::setw(10) << "Status" << std::endl;
    std::cout << std::string(104, '-') << std::endl;

    int totalTests = 0;
    int passedTests = 0;

    for (const auto& result : m_allResults) {
        bool packetsMatch = (result.totalPackets == result.expectedPackets);
        bool bytesMatch = (result.totalBytes == result.expectedBytes);
        bool noErrors = (result.sequenceErrors == 0 && 
                         result.checksumErrors == 0 && 
                         result.contentMismatches == 0);
        bool pass = result.success && packetsMatch && bytesMatch && noErrors;

        std::cout << std::left << std::setw(8) << result.role
                  << std::setw(8) << result.port
                  << std::setw(15) << std::fixed << std::setprecision(2) << result.duration
                  << std::setw(18) << result.throughput
                  << std::setw(22) << result.totalBytes
                  << std::setw(24) << result.totalPackets
                  << std::setw(10) << (pass ? "PASS" : "FAIL") << std::endl;

        if (!pass) {
            std::cout << "  -> Expected: " << result.expectedPackets << " packets (" 
                      << result.expectedBytes << " bytes)";
            if (!noErrors) {
                std::cout << ", Errors detected";
            }
            std::cout << std::endl;
        }

        if (pass) passedTests++;
        totalTests++;
    }

    std::cout << "\n--- Summary ---" << std::endl;
    std::cout << "Total Tests: " << totalTests << std::endl;
    std::cout << "Passed: " << passedTests << std::endl;
    std::cout << "Failed: " << (totalTests - passedTests) << std::endl;

    if (passedTests == totalTests) {
        std::cout << "\nSUCCESS: All tests passed!" << std::endl;
    } else {
        std::cout << "\nWARNING: Some tests failed." << std::endl;
    }
    
    std::cout << "==================================================" << std::endl;
}

} // namespace TestRunner2

