namespace Docs.Assets;

using System.IO;
using System.IO;
using System.Text;

private bool IsMarkdown(string name) {
    nuint length = name.Length;
    if (length < 3) {
        return false;
    }
    if (StringByteAt(name, length - 3) != 46) {
        return false;
    }
    if (StringByteAt(name, length - 2) != 109) {
        return false;
    }
    return StringByteAt(name, length - 1) == 100;
}

public Result<long, string> CountMarkdown(string path) {
    DirectoryStream directory = try NativeDirectoryOpen(path);
    long count = 0;
    bool active = true;
    while (active) {
        Result<string, string> next =
            NativeDirectoryNext(directory);
        switch (next) {
            case Result.Ok(name): {
                string view = name;
                if (IsMarkdown(view)) {
                    count = count + 1;
                }
            }
            case Result.Err(error): {
                if (error != "end of directory") {
                    return Result.Err(error);
                }
                active = false;
            }
        }
    }
    return Result.Ok(count);
}

public Result<string, string> LoadAsset(string path) {
    NativeHandle file = try NativeFileOpen(path, "rb");
    string contents = try NativeFileReadAll(file);
    return Result.Ok(contents);
}
