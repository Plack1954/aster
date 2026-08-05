using System.IO;

private Result<string, IoError> load(string path) {
    NativeHandle file = try NativeFileOpen(path, "rb");
    string contents = try NativeFileReadAll(file);
    return Result.Ok(contents);
}

int main() {
    Result<string, IoError> contents =
        load("examples/hello.as");
    switch (contents) {
        case Result.Ok(text): {
            Console.WriteLine(text);
        }
        case Result.Err(error): {
            Console.WriteLine(error);
        }
    }
    return 0;
}
