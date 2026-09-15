#include <source2root>

void Check(bool value, const char[] text) { if (!value) LogMessage(text); }
public bool OnPluginStart()
{
    Check(!ChangeMap("de_dust2"), "FAILED: startup map change");
    Check(!RestartRound(1), "FAILED: startup restart");
    return RegisterCommand("sr_server_api", "admin.changemap", Run)
        && RegisterCommand("sr_config_fault", "admin.changemap", Fault)
        && RegisterCommand("sr_queue_fault", "admin.changemap", QueueFault);
}
public void Run(Player caller, const char[] arguments)
{
    Check(!IsMapInstalled("../de_dust2") && !ChangeMap("de_dust2;quit") && !ChangeMap("")
        && !RestartRound(0) && !RestartRound(61), "FAILED: server native argument guards");
    Check(OpenConfigFile("../secret") == NoConfigFile && OpenConfigFile("/etc/passwd") == NoConfigFile
        && OpenConfigFile("outside.txt") == NoConfigFile && OpenConfigFile("missing.txt") == NoConfigFile
        && OpenConfigFile("oversized.txt") == NoConfigFile && OpenConfigFile("binary.txt") == NoConfigFile,
        "FAILED: configuration path, type or size guard");
    ConfigFile file = OpenConfigFile("snapshot.txt");
    Check(file != NoConfigFile, "FAILED: snapshot open");
    char small[3], line[32];
    Check(ReadConfigLine(file, small, sizeof(small)) == -1 && small[0] == 0, "FAILED: line was truncated");
    Check(ReadConfigLine(file, line, sizeof(line)) == 1 && line[0] == 'f', "FAILED: short-buffer retry");
    LogMessage("replace snapshot file");
    Check(ReadConfigLine(file, line, sizeof(line)) == 1 && line[0] == 0, "FAILED: empty CRLF line");
    Check(ReadConfigLine(file, line, sizeof(line)) == 1 && line[0] == 'l', "FAILED: immutable final line");
    Check(ReadConfigLine(file, line, sizeof(line)) == 0 && line[0] == 0, "FAILED: snapshot EOF");
    Check(CloseConfigFile(file), "FAILED: close snapshot");
    ConfigFile files[8];
    for (int i = 0; i < sizeof(files); i++) { files[i] = OpenConfigFile("snapshot.txt"); Check(files[i] != NoConfigFile, "FAILED: snapshot quota too small"); }
    Check(OpenConfigFile("snapshot.txt") == NoConfigFile, "FAILED: snapshot quota not bounded");
    for (int i = 0; i < sizeof(files); i++) Check(CloseConfigFile(files[i]), "FAILED: snapshot close");
    OpenConfigFile("snapshot.txt");
    LogMessage("server API checks passed");
}
public void Fault(Player caller, const char[] arguments)
{
    ConfigFile file = OpenConfigFile("snapshot.txt");
    CloseConfigFile(file);
    char line[32];
    ReadConfigLine(file, line, sizeof(line));
    LogMessage("FAILED: stale configuration handle accepted");
}

public void QueueFault(Player caller, const char[] arguments)
{
    Check(ChangeMap("de_dust2"), "FAILED: queue before fault");
    Fault(caller, arguments);
}
