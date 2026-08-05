using System.IO;
using Aster.Interop;
using System.Text;

private Result<bool, string> InspectLine(string line) {
    Console.WriteLine(line.Length);
    return Result.Ok(line != "stop");
}

private Result<int, string> run() {
    string path = try NativeProcessArg(0);
    nuint count = try ForEachLineBuffered(
        path,
        17,
        InspectLine,
    );
    Console.WriteLine(count);
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
