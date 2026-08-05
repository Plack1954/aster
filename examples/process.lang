using Aster.Interop;

int main() {
    Console.WriteLine(NativeProcessArgCount());

    Result<string, ProcessError> argument =
        NativeProcessArg(0);
    switch (argument) {
        case Result.Ok(value): {
            Console.WriteLine(value);
        }
        case Result.Err(error): {
            Console.WriteLine(error);
        }
    }

    Result<string, ProcessError> environment =
        NativeProcessEnvironment("ASTER_PROCESS_TEST");
    switch (environment) {
        case Result.Ok(value): {
            Console.WriteLine(value);
        }
        case Result.Err(error): {
            Console.WriteLine(error);
        }
    }
    return 0;
}
