namespace System.Diagnostics;

using Aster.Memory;

private extern Result<NativeHandle, string> NativeProcessCreate(
    string executable,
    string workingDirectory,
    bool redirectStandardInput,
    bool redirectStandardOutput,
    bool redirectStandardError
);
private extern Result<Unit, string> NativeProcessAddArgument(
    NativeHandle process,
    string argument
);
private extern Result<Unit, string> NativeProcessSetEnvironment(
    NativeHandle process,
    string name,
    string value
);
private extern Result<Unit, string> NativeProcessLaunch(NativeHandle process);
private extern Result<int, string> NativeProcessWait(NativeHandle process);
private extern Result<bool, string> NativeProcessHasExited(NativeHandle process);
private extern Result<int, string> NativeProcessExitCode(NativeHandle process);
private extern Result<nuint, string> NativeProcessWriteInput(
    NativeHandle process,
    ReadOnlySpan<byte> source
);
private extern Result<nuint, string> NativeProcessReadOutput(
    NativeHandle process,
    Span<byte> destination
);
private extern Result<nuint, string> NativeProcessReadError(
    NativeHandle process,
    Span<byte> destination
);
private extern Result<Unit, string> NativeProcessCloseInput(
    NativeHandle process
);
private extern Result<Unit, string> NativeProcessKill(NativeHandle process);

public struct ProcessEnvironmentVariable
{
    string Name;
    string Value;
}

public struct ProcessStartInfo
{
    string FileName;
    List<string> Arguments;
    string WorkingDirectory;
    List<ProcessEnvironmentVariable> Environment;
    bool RedirectStandardInput;
    bool RedirectStandardOutput;
    bool RedirectStandardError;
}

public struct Process
{
    NativeHandle Handle;
}

public ProcessStartInfo ProcessStartInfo(string fileName)
{
    if (fileName.Length == 0)
    {
        throw new ArgumentException("process file name cannot be empty");
    }
    return new()
    {
        FileName = fileName,
        Arguments = new(),
        WorkingDirectory = "",
        Environment = new(),
        RedirectStandardInput = false,
        RedirectStandardOutput = false,
        RedirectStandardError = false
    };
}

private T ProcessResultOrThrow<T>(Result<T, string> result)
{
    switch (result)
    {
        case Result.Ok(value): { return value; }
        case Result.Err(error): { throw new IOException(error); }
    }
}

public Process Process.Start(ProcessStartInfo startInfo)
{
    if (startInfo.FileName.Length == 0)
    {
        throw new ArgumentException("process file name cannot be empty");
    }
    NativeHandle handle = ProcessResultOrThrow(
        NativeProcessCreate(
            startInfo.FileName,
            startInfo.WorkingDirectory,
            startInfo.RedirectStandardInput,
            startInfo.RedirectStandardOutput,
            startInfo.RedirectStandardError
        )
    );
    for (nuint index = 0; index < startInfo.Arguments.Count; index++)
    {
        Unit ignored = ProcessResultOrThrow(
            NativeProcessAddArgument(handle, startInfo.Arguments[index])
        );
    }
    for (nuint index = 0; index < startInfo.Environment.Count; index++)
    {
        ProcessEnvironmentVariable variable = startInfo.Environment[index];
        if (variable.Name.Length == 0)
        {
            throw new ArgumentException(
                "process environment name cannot be empty"
            );
        }
        Unit ignored = ProcessResultOrThrow(
            NativeProcessSetEnvironment(
                handle, variable.Name, variable.Value
            )
        );
    }
    Unit launched = ProcessResultOrThrow(NativeProcessLaunch(handle));
    return new() { Handle = handle };
}

public Process Process.Start(string fileName)
{
    return Process.Start(ProcessStartInfo(fileName));
}

public bool Process.HasExited(Process self)
{
    return ProcessResultOrThrow(NativeProcessHasExited(self.Handle));
}

public int Process.WaitForExit(Process self)
{
    return ProcessResultOrThrow(NativeProcessWait(self.Handle));
}

public int Process.ExitCode(Process self)
{
    return ProcessResultOrThrow(NativeProcessExitCode(self.Handle));
}

public nuint Process.WriteStandardInput(
    Process self,
    ReadOnlySpan<byte> source
)
{
    return ProcessResultOrThrow(NativeProcessWriteInput(self.Handle, source));
}

public void Process.CloseStandardInput(Process self)
{
    Unit ignored = ProcessResultOrThrow(NativeProcessCloseInput(self.Handle));
}

public nuint Process.ReadStandardOutput(
    Process self,
    Span<byte> destination
)
{
    return ProcessResultOrThrow(
        NativeProcessReadOutput(self.Handle, destination)
    );
}

public nuint Process.ReadStandardError(
    Process self,
    Span<byte> destination
)
{
    return ProcessResultOrThrow(
        NativeProcessReadError(self.Handle, destination)
    );
}

public void Process.Kill(Process self)
{
    Unit ignored = ProcessResultOrThrow(NativeProcessKill(self.Handle));
}
