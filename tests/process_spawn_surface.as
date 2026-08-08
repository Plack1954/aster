using Aster.Memory;
using System.Diagnostics;
using System.Text;

private string ReadOutput(const ref Process process, bool error)
{
    Buffer buffer = Buffer.allocate(256);
    StringBuilder output = new();
    while (true)
    {
        nuint count = 0;
        unsafe
        {
            Span<byte> destination = BufferAsMutSlice(buffer);
            count = error
                ? process.ReadStandardError(destination)
                : process.ReadStandardOutput(destination);
            if (count != 0)
            {
                switch (ByteSliceToString(destination, 0, count))
                {
                    case Result.Ok(value): { output.Append(value); }
                    case Result.Err(failure): { return failure; }
                }
            }
        }
        if (count == 0) { return output.ToString(); }
    }
    return output.ToString();
}

int main()
{
    ProcessStartInfo info = ProcessStartInfo("/bin/sh");
    info.Arguments.Add("-c");
    info.Arguments.Add(
        "printf '%s|%s|' \"$ASTER_CHILD\" \"$PWD\"; cat; printf problem >&2; exit 7"
    );
    info.WorkingDirectory = "/tmp";
    info.Environment.Add(new() { Name = "ASTER_CHILD", Value = "value with spaces" });
    info.RedirectStandardInput = true;
    info.RedirectStandardOutput = true;
    info.RedirectStandardError = true;
    Process child = Process.Start(info);
    if (child.HasExited()) { return 1; }
    nuint written = child.WriteStandardInput(StringAsByteSlice("input bytes"));
    if (written != 11) { return 2; }
    child.CloseStandardInput();
    string output = ReadOutput(child, false);
    string error = ReadOutput(child, true);
    int exitCode = child.WaitForExit();
    if (exitCode != 7 || !child.HasExited() || child.ExitCode() != 7)
    {
        return 3;
    }
    if (output != "value with spaces|/tmp|input bytes") { return 4; }
    if (error != "problem") { return 5; }

    ProcessStartInfo sleepingInfo = ProcessStartInfo("/bin/sh");
    sleepingInfo.Arguments.Add("-c");
    sleepingInfo.Arguments.Add("sleep 10");
    Process sleeping = Process.Start(sleepingInfo);
    sleeping.Kill();
    if (sleeping.WaitForExit() != 143) { return 6; }

    bool invalidRejected = false;
    try
    {
        ProcessStartInfo invalidInfo = ProcessStartInfo(
            "/definitely/not/an/aster/executable"
        );
        Process invalid = Process.Start(invalidInfo);
    }
    catch (IOException failure)
    {
        invalidRejected = failure.Message.Contains("execute");
    }
    if (!invalidRejected) { return 7; }
    return 0;
}
