namespace Aster.Content;

using System.IO;
using System.Text;

private string ContentJoin(string root, string name)
{
    if (StringByteAt(root, root.Length - 1) == 47)
    {
        return string.Concat(root, name);
    }
    return string.Concat(root, "/", name);
}

private int ContentStringCompare(string left, string right)
{
    nuint shared = left.Length < right.Length
        ? left.Length : right.Length;
    for (nuint index = 0; index < shared; index++)
    {
        byte leftByte = StringByteAt(left, index);
        byte rightByte = StringByteAt(right, index);
        if (leftByte < rightByte) { return -1; }
        if (leftByte > rightByte) { return 1; }
    }
    if (left.Length < right.Length) { return -1; }
    if (left.Length > right.Length) { return 1; }
    return 0;
}

private bool ContentStringsContain(List<string> values, string wanted)
{
    foreach (string value in values)
    {
        if (value == wanted) { return true; }
    }
    return false;
}

// Returns matching regular files immediately below root in deterministic
// filename order. Content trees choose recursion and metadata policy
// explicitly rather than inheriting it from the filesystem primitives.
public Result<List<string>, string> DiscoverFiles(
    string root,
    string suffix
)
{
    if (root.Length == 0)
    {
        return Result.Err("content directory root cannot be empty");
    }
    switch (NativeDirectoryOpen(root))
    {
        case Result.Err(error): { return Result.Err(error); }
        case Result.Ok(directory): {
            List<string> names = new();
            for (;;)
            {
                switch (NativeDirectoryNext(directory))
                {
                    case Result.Err(error): {
                        if (error == "end of directory") { break; }
                        return Result.Err(error);
                    }
                    case Result.Ok(name): {
                        string path = ContentJoin(root, name);
                        switch (NativePathIsFile(path))
                        {
                            case Result.Err(error): {
                                return Result.Err(error);
                            }
                            case Result.Ok(isFile): {
                                if (name.EndsWith(suffix) && isFile)
                                {
                                    names.Add(name);
                                }
                            }
                        }
                    }
                }
            }

            List<string> emitted = new();
            List<string> paths = new();
            while (emitted.Count < names.Count)
            {
                bool found = false;
                string selected = "";
                foreach (string name in names)
                {
                    if (!ContentStringsContain(emitted, name) &&
                        (!found || ContentStringCompare(name, selected) < 0))
                    {
                        selected = name;
                        found = true;
                    }
                }
                emitted.Add(selected);
                paths.Add(ContentJoin(root, selected));
            }
            return Result.Ok(paths);
        }
    }
}
