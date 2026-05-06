#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

struct Options {
    bool help = false;
    fs::path file;
    fs::path resultDir;
    std::string runId;
    std::string role;
};

class ValidationError : public std::runtime_error {
public:
    explicit ValidationError(const std::string& message)
        : std::runtime_error(message) {}
};

std::string toUpper(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });
    return value;
}

bool normalizeRole(std::string& role) {
    role = toUpper(role);
    return role == "CLIENT" || role == "SERVER";
}

void printUsage(std::ostream& os) {
    os << "ResultJsonViewer - print a human-readable MyIperf result JSON summary\n\n"
       << "Usage:\n"
       << "  ResultJsonViewer --file <path>\n"
       << "  ResultJsonViewer --result-dir <path> --run-id <id> --role <CLIENT|SERVER>\n\n"
       << "Options:\n"
       << "  --file <path>        Read a specific result JSON file. Takes precedence.\n"
       << "  --result-dir <path>  Directory containing result-<runId>-<ROLE>.json.\n"
       << "  --run-id <id>        Run ID used in the result file name.\n"
       << "  --role <role>        CLIENT or SERVER. Case-insensitive.\n"
       << "  -h, --help           Show this help.\n\n"
       << "Exit codes:\n"
       << "  0  Result success is true and finalState is FINISHED.\n"
       << "  1  JSON was read, but the result verdict is FAIL.\n"
       << "  2  File open, parse, option, or schema validation error.\n";
}

bool readValue(int argc, char* argv[], int& index, const std::string& option, std::string& out, std::string& error) {
    if (index + 1 >= argc) {
        error = option + " requires a value";
        return false;
    }
    out = argv[++index];
    return true;
}

bool parseOptions(int argc, char* argv[], Options& options, std::string& error) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];

        if (arg == "-h" || arg == "--help") {
            options.help = true;
            continue;
        }

        std::string value;
        if (arg == "--file") {
            if (!readValue(argc, argv, i, arg, value, error)) {
                return false;
            }
            options.file = value;
        } else if (arg == "--result-dir") {
            if (!readValue(argc, argv, i, arg, value, error)) {
                return false;
            }
            options.resultDir = value;
        } else if (arg == "--run-id") {
            if (!readValue(argc, argv, i, arg, options.runId, error)) {
                return false;
            }
        } else if (arg == "--role") {
            if (!readValue(argc, argv, i, arg, options.role, error)) {
                return false;
            }
        } else {
            error = "Unknown option: " + arg;
            return false;
        }
    }

    if (options.help) {
        return true;
    }

    if (!options.role.empty() && !normalizeRole(options.role)) {
        error = "--role must be CLIENT or SERVER";
        return false;
    }

    if (!options.file.empty()) {
        return true;
    }

    if (options.resultDir.empty() || options.runId.empty() || options.role.empty()) {
        error = "Provide --file, or provide all of --result-dir, --run-id, and --role";
        return false;
    }

    options.file = options.resultDir / ("result-" + options.runId + "-" + options.role + ".json");
    return true;
}

const json& requireField(const json& object, const std::string& key, const std::string& path) {
    if (!object.is_object()) {
        throw ValidationError(path + " must be an object");
    }
    auto it = object.find(key);
    if (it == object.end()) {
        throw ValidationError("Missing required field: " + path + "." + key);
    }
    return *it;
}

const json& requireObject(const json& object, const std::string& key, const std::string& path) {
    const json& value = requireField(object, key, path);
    if (!value.is_object()) {
        throw ValidationError(path + "." + key + " must be an object");
    }
    return value;
}

std::string requireString(const json& object, const std::string& key, const std::string& path) {
    const json& value = requireField(object, key, path);
    if (!value.is_string()) {
        throw ValidationError(path + "." + key + " must be a string");
    }
    return value.get<std::string>();
}

bool requireBool(const json& object, const std::string& key, const std::string& path) {
    const json& value = requireField(object, key, path);
    if (!value.is_boolean()) {
        throw ValidationError(path + "." + key + " must be a boolean");
    }
    return value.get<bool>();
}

double requireNumber(const json& object, const std::string& key, const std::string& path) {
    const json& value = requireField(object, key, path);
    if (!value.is_number()) {
        throw ValidationError(path + "." + key + " must be a number");
    }
    return value.get<double>();
}

std::string optionalString(const json& object, const std::string& key, const std::string& fallback = "") {
    auto it = object.find(key);
    if (it == object.end() || it->is_null()) {
        return fallback;
    }
    if (!it->is_string()) {
        throw ValidationError(key + " must be a string when present");
    }
    return it->get<std::string>();
}

std::string numberText(double value, int precision = 3) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(precision) << value;
    return oss.str();
}

std::string integerText(double value) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(0) << value;
    return oss.str();
}

struct StatsView {
    double totalPacketsSent = 0.0;
    double totalPacketsReceived = 0.0;
    double totalBytesSent = 0.0;
    double totalBytesReceived = 0.0;
    double failedChecksumCount = 0.0;
    double sequenceErrorCount = 0.0;
    double contentMismatchCount = 0.0;
    double duration = 0.0;
    double throughputMbps = 0.0;
};

StatsView readStats(const json& stats, const std::string& path) {
    StatsView view;
    view.totalPacketsSent = requireNumber(stats, "totalPacketsSent", path);
    view.totalPacketsReceived = requireNumber(stats, "totalPacketsReceived", path);
    view.totalBytesSent = requireNumber(stats, "totalBytesSent", path);
    view.totalBytesReceived = requireNumber(stats, "totalBytesReceived", path);
    view.failedChecksumCount = requireNumber(stats, "failedChecksumCount", path);
    view.sequenceErrorCount = requireNumber(stats, "sequenceErrorCount", path);
    view.contentMismatchCount = requireNumber(stats, "contentMismatchCount", path);
    view.duration = requireNumber(stats, "duration", path);
    view.throughputMbps = requireNumber(stats, "throughputMbps", path);
    return view;
}

struct PhaseView {
    std::string phaseName;
    std::string senderRole;
    std::string receiverRole;
    bool success = false;
    StatsView senderStats;
    StatsView receiverStats;
};

PhaseView readPhase(const json& result, const std::string& key) {
    const std::string path = "$." + key;
    const json& phase = requireObject(result, key, "$");

    PhaseView view;
    view.phaseName = requireString(phase, "phaseName", path);
    view.senderRole = requireString(phase, "senderRole", path);
    view.receiverRole = requireString(phase, "receiverRole", path);
    view.success = requireBool(phase, "success", path);
    view.senderStats = readStats(requireObject(phase, "senderStats", path), path + ".senderStats");
    view.receiverStats = readStats(requireObject(phase, "receiverStats", path), path + ".receiverStats");
    return view;
}

struct ResultView {
    std::string runId;
    std::string role;
    std::string schemaVersion;
    std::string startedAt;
    std::string finishedAt;
    std::string finalState;
    std::string failureReason;
    std::string resultExportWarning;
    bool success = false;
    json config;
    PhaseView phase1;
    PhaseView phase2;
};

ResultView validateAndReadResult(const json& result) {
    if (!result.is_object()) {
        throw ValidationError("Top-level JSON value must be an object");
    }

    ResultView view;
    view.runId = requireString(result, "runId", "$");
    view.role = requireString(result, "role", "$");
    if (!normalizeRole(view.role)) {
        throw ValidationError("$.role must be CLIENT or SERVER");
    }
    view.success = requireBool(result, "success", "$");
    view.finalState = requireString(result, "finalState", "$");
    view.config = requireObject(result, "config", "$");
    view.phase1 = readPhase(result, "phase1");
    view.phase2 = readPhase(result, "phase2");

    view.schemaVersion = optionalString(result, "schemaVersion", "");
    view.startedAt = optionalString(result, "startedAt", "");
    view.finishedAt = optionalString(result, "finishedAt", "");
    view.failureReason = optionalString(result, "failureReason", "");
    view.resultExportWarning = optionalString(result, "resultExportWarning", "");

    requireString(view.config, "targetIP", "$.config");
    requireNumber(view.config, "port", "$.config");
    requireNumber(view.config, "packetSize", "$.config");
    requireNumber(view.config, "numPackets", "$.config");
    requireNumber(view.config, "sendIntervalMs", "$.config");
    requireString(view.config, "protocol", "$.config");

    return view;
}

void printKeyValue(std::ostream& os, const std::string& key, const std::string& value) {
    os << std::left << std::setw(22) << key << ": " << value << '\n';
}

void printPhaseRow(std::ostream& os, int number, const PhaseView& phase) {
    const std::string verdict = phase.success ? "PASS" : "FAIL";
    const std::string senderTraffic = integerText(phase.senderStats.totalPacketsSent) + " / " +
                                      integerText(phase.senderStats.totalBytesSent);
    const std::string receiverTraffic = integerText(phase.receiverStats.totalPacketsReceived) + " / " +
                                        integerText(phase.receiverStats.totalBytesReceived);
    const std::string mismatches = integerText(phase.receiverStats.failedChecksumCount) + " / " +
                                   integerText(phase.receiverStats.sequenceErrorCount) + " / " +
                                   integerText(phase.receiverStats.contentMismatchCount);

    os << std::left << std::setw(7) << number
       << std::setw(22) << phase.phaseName
       << std::setw(12) << phase.senderRole
       << std::setw(12) << phase.receiverRole
       << std::setw(9) << verdict
       << std::setw(18) << senderTraffic
       << std::setw(18) << receiverTraffic
       << std::setw(18) << numberText(phase.receiverStats.throughputMbps)
       << mismatches << '\n';
}

void printSummary(const ResultView& result) {
    const bool passed = result.success && result.finalState == "FINISHED";

    std::cout << "=== MyIperf Result Summary ===\n";
    std::cout << "Status: " << (passed ? "PASS" : "FAIL") << "\n\n";

    printKeyValue(std::cout, "runId", result.runId);
    printKeyValue(std::cout, "role", result.role);
    printKeyValue(std::cout, "schemaVersion", result.schemaVersion);
    printKeyValue(std::cout, "startedAt", result.startedAt);
    printKeyValue(std::cout, "finishedAt", result.finishedAt);
    printKeyValue(std::cout, "finalState", result.finalState);

    if (!result.failureReason.empty()) {
        printKeyValue(std::cout, "failureReason", result.failureReason);
    }
    if (!result.resultExportWarning.empty()) {
        printKeyValue(std::cout, "resultExportWarning", result.resultExportWarning);
    }

    std::cout << "\nConfig\n";
    printKeyValue(std::cout, "targetIP", result.config.at("targetIP").get<std::string>());
    printKeyValue(std::cout, "port", integerText(result.config.at("port").get<double>()));
    printKeyValue(std::cout, "packetSize", integerText(result.config.at("packetSize").get<double>()));
    printKeyValue(std::cout, "numPackets", integerText(result.config.at("numPackets").get<double>()));
    printKeyValue(std::cout, "intervalMs", integerText(result.config.at("sendIntervalMs").get<double>()));
    printKeyValue(std::cout, "protocol", result.config.at("protocol").get<std::string>());

    std::cout << "\nPhase Summary\n";
    std::cout << std::left << std::setw(7) << "Phase"
              << std::setw(22) << "Name"
              << std::setw(12) << "Sender"
              << std::setw(12) << "Receiver"
              << std::setw(9) << "Success"
              << std::setw(18) << "Snd pkt/bytes"
              << std::setw(18) << "Rcv pkt/bytes"
              << std::setw(18) << "Rcv Mbps"
              << "Chk/Seq/Content\n";

    printPhaseRow(std::cout, 1, result.phase1);
    printPhaseRow(std::cout, 2, result.phase2);

    std::cout << "\nPhase 1 (" << result.phase1.phaseName << "): " << (result.phase1.success ? "PASS" : "FAIL") << '\n';
    std::cout << "Phase 2 (" << result.phase2.phaseName << "): " << (result.phase2.success ? "PASS" : "FAIL") << '\n';
}

json loadJsonFile(const fs::path& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("Cannot open file: " + path.string());
    }

    try {
        json value;
        input >> value;
        return value;
    } catch (const json::parse_error& ex) {
        throw std::runtime_error(std::string("JSON parse error: ") + ex.what());
    }
}

} // namespace

int main(int argc, char* argv[]) {
    Options options;
    std::string error;
    if (!parseOptions(argc, argv, options, error)) {
        std::cerr << "Error: " << error << "\n\n";
        printUsage(std::cerr);
        return 2;
    }

    if (options.help) {
        printUsage(std::cout);
        return 0;
    }

    try {
        const json resultJson = loadJsonFile(options.file);
        const ResultView result = validateAndReadResult(resultJson);
        printSummary(result);
        return result.success && result.finalState == "FINISHED" ? 0 : 1;
    } catch (const ValidationError& ex) {
        std::cerr << "Schema error: " << ex.what() << '\n';
        return 2;
    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << '\n';
        return 2;
    }
}
