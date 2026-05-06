#pragma once

#include "myiperf/Config.h"
#include "myiperf/Protocol.h"

#include "nlohmann/json.hpp"

#include <string>

struct TestPhaseResult {
    std::string phaseName;
    std::string senderRole;
    std::string receiverRole;
    TestStats senderStats;
    TestStats receiverStats;
    bool success = false;
    std::string failureReason;
};

struct TestRunResult {
    std::string schemaVersion = "1";
    std::string runId;
    std::string role;
    std::string startedAt;
    std::string finishedAt;
    std::string finalState;
    bool success = false;
    std::string failureReason;
    std::string resultExportWarning;
    Config config;
    TestPhaseResult phase1;
    TestPhaseResult phase2;
};

namespace nlohmann {

template <>
struct adl_serializer<TestPhaseResult> {
    static void to_json(json& j, const TestPhaseResult& p) {
        j = json{
            {"phaseName", p.phaseName},
            {"senderRole", p.senderRole},
            {"receiverRole", p.receiverRole},
            {"senderStats", p.senderStats},
            {"receiverStats", p.receiverStats},
            {"success", p.success},
            {"failureReason", p.failureReason},
        };
    }

    static void from_json(const json& j, TestPhaseResult& p) {
        j.at("phaseName").get_to(p.phaseName);
        j.at("senderRole").get_to(p.senderRole);
        j.at("receiverRole").get_to(p.receiverRole);
        j.at("senderStats").get_to(p.senderStats);
        j.at("receiverStats").get_to(p.receiverStats);
        j.at("success").get_to(p.success);
        j.at("failureReason").get_to(p.failureReason);
    }
};

template <>
struct adl_serializer<TestRunResult> {
    static void to_json(json& j, const TestRunResult& r) {
        j = json{
            {"schemaVersion", r.schemaVersion},
            {"runId", r.runId},
            {"role", r.role},
            {"startedAt", r.startedAt},
            {"finishedAt", r.finishedAt},
            {"finalState", r.finalState},
            {"success", r.success},
            {"failureReason", r.failureReason},
            {"resultExportWarning", r.resultExportWarning},
            {"config", r.config.toJson()},
            {"phase1", r.phase1},
            {"phase2", r.phase2},
        };
    }

    static void from_json(const json& j, TestRunResult& r) {
        j.at("schemaVersion").get_to(r.schemaVersion);
        j.at("runId").get_to(r.runId);
        j.at("role").get_to(r.role);
        j.at("startedAt").get_to(r.startedAt);
        j.at("finishedAt").get_to(r.finishedAt);
        j.at("finalState").get_to(r.finalState);
        j.at("success").get_to(r.success);
        j.at("failureReason").get_to(r.failureReason);
        if (j.contains("resultExportWarning")) {
            j.at("resultExportWarning").get_to(r.resultExportWarning);
        }
        r.config = Config::fromJson(j.at("config"));
        j.at("phase1").get_to(r.phase1);
        j.at("phase2").get_to(r.phase2);
    }
};

} // namespace nlohmann
