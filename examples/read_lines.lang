using System.IO;
using Aster.Interop;
using System.Text;

private Result<int, string> run() {
    string path = try NativeProcessArg(0);
    List<string> lines =
        try ReadLinesBuffered(path, 31);
    foreach (string line in lines) {
        Console.WriteLine(line);
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
