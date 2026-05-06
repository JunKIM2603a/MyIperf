using System;
using System.Globalization;
using System.IO;
using System.Text.Json;

namespace ResultJsonViewerCSharp;

internal sealed class Options
{
    public bool Help { get; set; }
    public string? File { get; set; }
    public string? ResultDir { get; set; }
    public string? RunId { get; set; }
    public string? Role { get; set; }
}

internal sealed class ValidationException : Exception
{
    public ValidationException(string message)
        : base(message)
    {
    }
}

internal sealed class StatsView
{
    public double TotalPacketsSent { get; set; }
    public double TotalPacketsReceived { get; set; }
    public double TotalBytesSent { get; set; }
    public double TotalBytesReceived { get; set; }
    public double FailedChecksumCount { get; set; }
    public double SequenceErrorCount { get; set; }
    public double ContentMismatchCount { get; set; }
    public double Duration { get; set; }
    public double ThroughputMbps { get; set; }
}

internal sealed class PhaseView
{
    public string PhaseName { get; set; } = "";
    public string SenderRole { get; set; } = "";
    public string ReceiverRole { get; set; } = "";
    public bool Success { get; set; }
    public StatsView SenderStats { get; set; } = new();
    public StatsView ReceiverStats { get; set; } = new();
}

internal sealed class ConfigView
{
    public string TargetIP { get; set; } = "";
    public double Port { get; set; }
    public double PacketSize { get; set; }
    public double NumPackets { get; set; }
    public double SendIntervalMs { get; set; }
    public string Protocol { get; set; } = "";
}

internal sealed class ResultView
{
    public string RunId { get; set; } = "";
    public string Role { get; set; } = "";
    public string SchemaVersion { get; set; } = "";
    public string StartedAt { get; set; } = "";
    public string FinishedAt { get; set; } = "";
    public string FinalState { get; set; } = "";
    public string FailureReason { get; set; } = "";
    public string ResultExportWarning { get; set; } = "";
    public bool Success { get; set; }
    public ConfigView Config { get; set; } = new();
    public PhaseView Phase1 { get; set; } = new();
    public PhaseView Phase2 { get; set; } = new();
}

internal static class Program
{
    private static int Main(string[] args)
    {
        if (!ParseOptions(args, out Options options, out string optionError))
        {
            Console.Error.WriteLine("Error: " + optionError);
            Console.Error.WriteLine();
            PrintUsage(Console.Error);
            return 2;
        }

        if (options.Help)
        {
            PrintUsage(Console.Out);
            return 0;
        }

        try
        {
            using JsonDocument document = LoadJsonFile(options.File!);
            ResultView result = ValidateAndReadResult(document.RootElement);
            PrintSummary(result);
            return result.Success && result.FinalState == "FINISHED" ? 0 : 1;
        }
        catch (ValidationException ex)
        {
            Console.Error.WriteLine("Schema error: " + ex.Message);
            return 2;
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or JsonException)
        {
            Console.Error.WriteLine("Error: " + ex.Message);
            return 2;
        }
    }

    private static void PrintUsage(TextWriter writer)
    {
        writer.WriteLine("ResultJsonViewer C# - print a human-readable MyIperf result JSON summary");
        writer.WriteLine();
        writer.WriteLine("Usage:");
        writer.WriteLine("  ResultJsonViewer --file <path>");
        writer.WriteLine("  ResultJsonViewer --result-dir <path> --run-id <id> --role <CLIENT|SERVER>");
        writer.WriteLine();
        writer.WriteLine("Options:");
        writer.WriteLine("  --file <path>        Read a specific result JSON file. Takes precedence.");
        writer.WriteLine("  --result-dir <path>  Directory containing result-<runId>-<ROLE>.json.");
        writer.WriteLine("  --run-id <id>        Run ID used in the result file name.");
        writer.WriteLine("  --role <role>        CLIENT or SERVER. Case-insensitive.");
        writer.WriteLine("  -h, --help           Show this help.");
        writer.WriteLine();
        writer.WriteLine("Exit codes:");
        writer.WriteLine("  0  Result success is true and finalState is FINISHED.");
        writer.WriteLine("  1  JSON was read, but the result verdict is FAIL.");
        writer.WriteLine("  2  File open, parse, option, or schema validation error.");
    }

    private static bool ParseOptions(string[] args, out Options options, out string error)
    {
        options = new Options();
        error = "";

        for (int i = 0; i < args.Length; ++i)
        {
            string arg = args[i];

            if (arg == "-h" || arg == "--help")
            {
                options.Help = true;
                continue;
            }

            if (arg == "--file")
            {
                if (!ReadValue(args, ref i, arg, out string? value, out error))
                {
                    return false;
                }
                options.File = value;
            }
            else if (arg == "--result-dir")
            {
                if (!ReadValue(args, ref i, arg, out string? value, out error))
                {
                    return false;
                }
                options.ResultDir = value;
            }
            else if (arg == "--run-id")
            {
                if (!ReadValue(args, ref i, arg, out string? value, out error))
                {
                    return false;
                }
                options.RunId = value;
            }
            else if (arg == "--role")
            {
                if (!ReadValue(args, ref i, arg, out string? value, out error))
                {
                    return false;
                }
                options.Role = value;
            }
            else
            {
                error = "Unknown option: " + arg;
                return false;
            }
        }

        if (options.Help)
        {
            return true;
        }

        if (!string.IsNullOrEmpty(options.Role))
        {
            options.Role = NormalizeRole(options.Role);
            if (options.Role is not ("CLIENT" or "SERVER"))
            {
                error = "--role must be CLIENT or SERVER";
                return false;
            }
        }

        if (!string.IsNullOrEmpty(options.File))
        {
            return true;
        }

        if (string.IsNullOrEmpty(options.ResultDir) ||
            string.IsNullOrEmpty(options.RunId) ||
            string.IsNullOrEmpty(options.Role))
        {
            error = "Provide --file, or provide all of --result-dir, --run-id, and --role";
            return false;
        }

        options.File = Path.Combine(options.ResultDir, $"result-{options.RunId}-{options.Role}.json");
        return true;
    }

    private static bool ReadValue(string[] args, ref int index, string option, out string? value, out string error)
    {
        if (index + 1 >= args.Length)
        {
            value = null;
            error = option + " requires a value";
            return false;
        }

        value = args[++index];
        error = "";
        return true;
    }

    private static string NormalizeRole(string role)
    {
        return role.ToUpperInvariant();
    }

    private static JsonDocument LoadJsonFile(string path)
    {
        try
        {
            using FileStream stream = File.OpenRead(path);
            return JsonDocument.Parse(stream);
        }
        catch (FileNotFoundException)
        {
            throw new IOException("Cannot open file: " + path);
        }
        catch (DirectoryNotFoundException)
        {
            throw new IOException("Cannot open file: " + path);
        }
    }

    private static ResultView ValidateAndReadResult(JsonElement result)
    {
        if (result.ValueKind != JsonValueKind.Object)
        {
            throw new ValidationException("Top-level JSON value must be an object");
        }

        ResultView view = new()
        {
            RunId = RequireString(result, "runId", "$"),
            Role = RequireString(result, "role", "$"),
            Success = RequireBool(result, "success", "$"),
            FinalState = RequireString(result, "finalState", "$"),
            SchemaVersion = OptionalString(result, "schemaVersion"),
            StartedAt = OptionalString(result, "startedAt"),
            FinishedAt = OptionalString(result, "finishedAt"),
            FailureReason = OptionalString(result, "failureReason"),
            ResultExportWarning = OptionalString(result, "resultExportWarning")
        };

        view.Role = NormalizeRole(view.Role);
        if (view.Role is not ("CLIENT" or "SERVER"))
        {
            throw new ValidationException("$.role must be CLIENT or SERVER");
        }

        JsonElement config = RequireObject(result, "config", "$");
        view.Config = new ConfigView
        {
            TargetIP = RequireString(config, "targetIP", "$.config"),
            Port = RequireNumber(config, "port", "$.config"),
            PacketSize = RequireNumber(config, "packetSize", "$.config"),
            NumPackets = RequireNumber(config, "numPackets", "$.config"),
            SendIntervalMs = RequireNumber(config, "sendIntervalMs", "$.config"),
            Protocol = RequireString(config, "protocol", "$.config")
        };

        view.Phase1 = ReadPhase(result, "phase1");
        view.Phase2 = ReadPhase(result, "phase2");
        return view;
    }

    private static PhaseView ReadPhase(JsonElement result, string key)
    {
        string path = "$." + key;
        JsonElement phase = RequireObject(result, key, "$");

        return new PhaseView
        {
            PhaseName = RequireString(phase, "phaseName", path),
            SenderRole = RequireString(phase, "senderRole", path),
            ReceiverRole = RequireString(phase, "receiverRole", path),
            Success = RequireBool(phase, "success", path),
            SenderStats = ReadStats(RequireObject(phase, "senderStats", path), path + ".senderStats"),
            ReceiverStats = ReadStats(RequireObject(phase, "receiverStats", path), path + ".receiverStats")
        };
    }

    private static StatsView ReadStats(JsonElement stats, string path)
    {
        return new StatsView
        {
            TotalPacketsSent = RequireNumber(stats, "totalPacketsSent", path),
            TotalPacketsReceived = RequireNumber(stats, "totalPacketsReceived", path),
            TotalBytesSent = RequireNumber(stats, "totalBytesSent", path),
            TotalBytesReceived = RequireNumber(stats, "totalBytesReceived", path),
            FailedChecksumCount = RequireNumber(stats, "failedChecksumCount", path),
            SequenceErrorCount = RequireNumber(stats, "sequenceErrorCount", path),
            ContentMismatchCount = RequireNumber(stats, "contentMismatchCount", path),
            Duration = RequireNumber(stats, "duration", path),
            ThroughputMbps = RequireNumber(stats, "throughputMbps", path)
        };
    }

    private static JsonElement RequireField(JsonElement obj, string key, string path)
    {
        if (obj.ValueKind != JsonValueKind.Object)
        {
            throw new ValidationException(path + " must be an object");
        }

        if (!obj.TryGetProperty(key, out JsonElement value))
        {
            throw new ValidationException("Missing required field: " + path + "." + key);
        }

        return value;
    }

    private static JsonElement RequireObject(JsonElement obj, string key, string path)
    {
        JsonElement value = RequireField(obj, key, path);
        if (value.ValueKind != JsonValueKind.Object)
        {
            throw new ValidationException(path + "." + key + " must be an object");
        }

        return value;
    }

    private static string RequireString(JsonElement obj, string key, string path)
    {
        JsonElement value = RequireField(obj, key, path);
        if (value.ValueKind != JsonValueKind.String)
        {
            throw new ValidationException(path + "." + key + " must be a string");
        }

        return value.GetString() ?? "";
    }

    private static bool RequireBool(JsonElement obj, string key, string path)
    {
        JsonElement value = RequireField(obj, key, path);
        if (value.ValueKind is not JsonValueKind.True and not JsonValueKind.False)
        {
            throw new ValidationException(path + "." + key + " must be a boolean");
        }

        return value.GetBoolean();
    }

    private static double RequireNumber(JsonElement obj, string key, string path)
    {
        JsonElement value = RequireField(obj, key, path);
        if (value.ValueKind != JsonValueKind.Number)
        {
            throw new ValidationException(path + "." + key + " must be a number");
        }

        return value.GetDouble();
    }

    private static string OptionalString(JsonElement obj, string key)
    {
        if (!obj.TryGetProperty(key, out JsonElement value) || value.ValueKind == JsonValueKind.Null)
        {
            return "";
        }

        if (value.ValueKind != JsonValueKind.String)
        {
            throw new ValidationException(key + " must be a string when present");
        }

        return value.GetString() ?? "";
    }

    private static void PrintSummary(ResultView result)
    {
        bool passed = result.Success && result.FinalState == "FINISHED";

        Console.WriteLine("=== MyIperf Result Summary ===");
        Console.WriteLine("Status: " + (passed ? "PASS" : "FAIL"));
        Console.WriteLine();

        PrintKeyValue("runId", result.RunId);
        PrintKeyValue("role", result.Role);
        PrintKeyValue("schemaVersion", result.SchemaVersion);
        PrintKeyValue("startedAt", result.StartedAt);
        PrintKeyValue("finishedAt", result.FinishedAt);
        PrintKeyValue("finalState", result.FinalState);

        if (!string.IsNullOrEmpty(result.FailureReason))
        {
            PrintKeyValue("failureReason", result.FailureReason);
        }

        if (!string.IsNullOrEmpty(result.ResultExportWarning))
        {
            PrintKeyValue("resultExportWarning", result.ResultExportWarning);
        }

        Console.WriteLine();
        Console.WriteLine("Config");
        PrintKeyValue("targetIP", result.Config.TargetIP);
        PrintKeyValue("port", IntegerText(result.Config.Port));
        PrintKeyValue("packetSize", IntegerText(result.Config.PacketSize));
        PrintKeyValue("numPackets", IntegerText(result.Config.NumPackets));
        PrintKeyValue("intervalMs", IntegerText(result.Config.SendIntervalMs));
        PrintKeyValue("protocol", result.Config.Protocol);

        Console.WriteLine();
        Console.WriteLine("Phase Summary");
        Console.Write("Phase".PadRight(7));
        Console.Write("Name".PadRight(22));
        Console.Write("Sender".PadRight(12));
        Console.Write("Receiver".PadRight(12));
        Console.Write("Success".PadRight(9));
        Console.Write("Snd pkt/bytes".PadRight(18));
        Console.Write("Rcv pkt/bytes".PadRight(18));
        Console.Write("Rcv Mbps".PadRight(18));
        Console.WriteLine("Chk/Seq/Content");

        PrintPhaseRow(1, result.Phase1);
        PrintPhaseRow(2, result.Phase2);

        Console.WriteLine();
        Console.WriteLine($"Phase 1 ({result.Phase1.PhaseName}): {(result.Phase1.Success ? "PASS" : "FAIL")}");
        Console.WriteLine($"Phase 2 ({result.Phase2.PhaseName}): {(result.Phase2.Success ? "PASS" : "FAIL")}");
    }

    private static void PrintKeyValue(string key, string value)
    {
        Console.WriteLine(key.PadRight(22) + ": " + value);
    }

    private static void PrintPhaseRow(int number, PhaseView phase)
    {
        string verdict = phase.Success ? "PASS" : "FAIL";
        string senderTraffic = IntegerText(phase.SenderStats.TotalPacketsSent) + " / " +
                               IntegerText(phase.SenderStats.TotalBytesSent);
        string receiverTraffic = IntegerText(phase.ReceiverStats.TotalPacketsReceived) + " / " +
                                 IntegerText(phase.ReceiverStats.TotalBytesReceived);
        string mismatches = IntegerText(phase.ReceiverStats.FailedChecksumCount) + " / " +
                            IntegerText(phase.ReceiverStats.SequenceErrorCount) + " / " +
                            IntegerText(phase.ReceiverStats.ContentMismatchCount);

        Console.Write(number.ToString(CultureInfo.InvariantCulture).PadRight(7));
        Console.Write(phase.PhaseName.PadRight(22));
        Console.Write(phase.SenderRole.PadRight(12));
        Console.Write(phase.ReceiverRole.PadRight(12));
        Console.Write(verdict.PadRight(9));
        Console.Write(senderTraffic.PadRight(18));
        Console.Write(receiverTraffic.PadRight(18));
        Console.Write(NumberText(phase.ReceiverStats.ThroughputMbps).PadRight(18));
        Console.WriteLine(mismatches);
    }

    private static string NumberText(double value)
    {
        return value.ToString("F3", CultureInfo.InvariantCulture);
    }

    private static string IntegerText(double value)
    {
        return value.ToString("F0", CultureInfo.InvariantCulture);
    }
}
