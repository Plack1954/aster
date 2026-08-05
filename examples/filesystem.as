using System.IO;
using Aster.Interop;

private Result<int, string> exercise() {
    string directory = try NativeProcessArg(0);
    string firstFile = try NativeProcessArg(1);
    string secondFile = try NativeProcessArg(2);
    string directoryPath = directory;
    string firstPath = firstFile;
    string secondPath = secondFile;

    Directory.CreateDirectory(directoryPath);
    File.WriteAllText(firstPath, "aster filesystem");

    bool isFile = File.Exists(firstPath);
    bool isDirectory = Directory.Exists(directoryPath);
    string contents = File.ReadAllText(firstPath);
    Console.WriteLine(isFile);
    Console.WriteLine(isDirectory);
    Console.WriteLine(contents.Length);

    File.Copy(firstPath, secondPath);
    File.Delete(firstPath);
    bool oldExists = File.Exists(firstPath);
    bool newExists = File.Exists(secondPath);
    Console.WriteLine(oldExists);
    Console.WriteLine(newExists);

    File.Move(secondPath, firstPath);
    File.Delete(firstPath);
    Directory.Delete(directoryPath);
    return Result.Ok(0);
}

int main() {
    Result<int, string> result = exercise();
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
