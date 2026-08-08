namespace Aster.CommandLine;

using Aster.Interop;
using System.Text;

public union CliArgument {
    Flag(string),
    Named(string),
    Positional(string),
}

public string CliNamedName(const ref string value) {
    switch (StringFindByte(value, 61)) {
        case Option.Some(position): {
            return StringSlice(value, 0, position);
        }
        case Option.None: {
            return "";
        }
    }
    return "";
}

public string CliNamedValue(const ref string value) {
    nuint length = value.Length;
    switch (StringFindByte(value, 61)) {
        case Option.Some(position): {
            return StringSlice(value, position + 1, length);
        }
        case Option.None: {
            return "";
        }
    }
    return "";
}

public Result<List<CliArgument>, string> CliParseProcessArguments() {
    List<CliArgument> output = new();
    nuint index = 0;
    nuint count = NativeProcessArgCount();
    bool optionsEnabled = true;

    while (index < count) {
        string argument = "";
        switch (NativeProcessArg(index)) {
            case Result.Err(error): { return Result.Err(error); }
            case Result.Ok(value): { argument = value; }
        }
        nuint length = argument.Length;

        if (optionsEnabled) {
            if (argument == "--") {
                optionsEnabled = false;
            } else if (argument.StartsWith("--")) {
                Option<nuint> separator =
                    StringFindByte(argument, 61);
                switch (separator) {
                    case Option.Some(position): {
                        if (position <= 2) {
                            return Result.Err(
                                "long option name cannot be empty"
                            );
                        }
                        string named =
                            StringSlice(argument, 2, length)
                        ;
                        output.Add(CliArgument.Named(named),
                        );
                    }
                    case Option.None: {
                        if (length <= 2) {
                            return Result.Err(
                                "long flag name cannot be empty"
                            );
                        }
                        string name =
                            StringSlice(argument, 2, length)
                        ;
                        output.Add(CliArgument.Flag(name),
                        );
                    }
                }
            } else if (argument.StartsWith("-")) {
                if (length > 1) {
                    string name =
                        StringSlice(argument, 1, length);
                    output.Add(CliArgument.Flag(name),
                    );
                } else {
                    output.Add(CliArgument.Positional(argument),
                    );
                }
            } else {
                output.Add(CliArgument.Positional(argument),
                );
            }
        } else {
            output.Add(CliArgument.Positional(argument),
            );
        }
        index = index + 1;
    }
    return Result.Ok(output);
}
