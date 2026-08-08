using Aster.CommandLine;
using System.Text;

private Result<int, string> run() {
    List<CliArgument> arguments = new();
    switch (CliParseProcessArguments()) {
        case Result.Err(error): {
            return Result.Err(error);
        }
        case Result.Ok(value): {
            arguments = value;
        }
    }
    foreach (CliArgument argument in arguments) {
        switch (argument) {
            case CliArgument.Flag(name): {
                Console.WriteLine("flag");
                Console.WriteLine(name);
            }
            case CliArgument.Named(named): {
                Console.WriteLine("named");
                Console.WriteLine(CliNamedName(named));
                Console.WriteLine(CliNamedValue(named));
            }
            case CliArgument.Positional(value): {
                Console.WriteLine("positional");
                Console.WriteLine(value);
            }
        }
    }
    return Result.Ok(0);
}

int main() {
    Result<int, string> result = run();
    switch (result) {
        case Result.Ok(status): {
            return status;
        }
        case Result.Err(error): {
            Console.WriteLine(error);
            return 1;
        }
    }
    return 1;
}
