namespace Aster.Interop;

public using ProcessError = string;

// Arguments exclude the `lang` executable, command, source path, and optional
// `--` separator. Returned strings own copies of host process data.
public extern nuint NativeProcessArgCount();

public extern Result<string, ProcessError> NativeProcessArg(
    nuint index
);

public extern Result<string, ProcessError> NativeProcessEnvironment(
    string name
);
