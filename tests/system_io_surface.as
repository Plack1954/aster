using System.IO;

private string CreateScopedTemporaryFile(string directory)
{
    switch (NativeFileCreateTemporary(directory))
    {
        case Result.Ok(file): {
            string path = NativeFileTemporaryPath(file);
            if (!File.Exists(path))
            {
                throw new IOException("temporary file was not created");
            }
            return path;
        }
        case Result.Err(error): { throw new IOException(error); }
    }
}

int main()
{
    if (Path.Combine("/srv", "aster") != "/srv/aster") { return 11; }
    if (Path.Combine("/srv", "aster", "site") !=
        "/srv/aster/site") { return 12; }
    if (Path.Combine("a", "b", "c", "d") != "a/b/c/d") { return 13; }
    if (Path.Combine("ignored", "/absolute") != "/absolute") { return 14; }
    if (Path.Combine("", "relative") != "relative") { return 15; }
    if (Path.Join("root", "/child") != "root/child") { return 16; }
    if (Path.Join("a", "b", "c", "d") != "a/b/c/d") { return 17; }
    if (Path.GetFileName("/tmp/archive.tar.gz") != "archive.tar.gz")
        { return 18; }
    if (Path.GetFileNameWithoutExtension("/tmp/archive.tar.gz") !=
        "archive.tar") { return 19; }
    if (Path.GetExtension("/tmp/archive.tar.gz") != ".gz") { return 20; }
    if (Path.GetExtension("/tmp/readme") != "") { return 21; }
    if (Path.GetFileName("/tmp/") != "") { return 22; }
    if (!Path.IsPathFullyQualified("/tmp/aster")) { return 23; }
    if (Path.IsPathFullyQualified("tmp/aster")) { return 24; }
    string? directoryName = Path.GetDirectoryName("/tmp/aster/file.txt");
    if (directoryName == null || directoryName.Value != "/tmp/aster")
        { return 33; }
    string? noDirectory = Path.GetDirectoryName("file.txt");
    if (noDirectory == null || noDirectory.Value != "") { return 34; }
    if (Path.GetDirectoryName("/") != null) { return 35; }
    string? pathRoot = Path.GetPathRoot("/tmp/aster");
    if (pathRoot == null || pathRoot.Value != "/") { return 36; }
    string? relativeRoot = Path.GetPathRoot("tmp/aster");
    if (relativeRoot == null || relativeRoot.Value != "") { return 37; }
    if (Path.GetPathRoot("") != null) { return 38; }
    if (Path.ChangeExtension("archive.tar.gz", ".zip") !=
        "archive.tar.zip") { return 39; }
    if (Path.ChangeExtension("archive", "txt") != "archive.txt")
        { return 40; }
    if (Path.ChangeExtension("archive.txt", "") != "archive.")
        { return 41; }
    string? removeExtension = null;
    if (Path.ChangeExtension("archive.tar.gz", removeExtension) !=
        "archive.tar") { return 42; }

    string directory = "/tmp/aster-system-io-surface";
    string first = "/tmp/aster-system-io-surface/first.txt";
    string second = "/tmp/aster-system-io-surface/second.txt";
    string child = "/tmp/aster-system-io-surface/child";

    if (File.Exists(first)) { File.Delete(first); }
    if (File.Exists(second)) { File.Delete(second); }
    if (Directory.Exists(child)) { Directory.Delete(child); }
    if (Directory.Exists(directory)) { Directory.Delete(directory); }

    Directory.CreateDirectory(directory);
    if (!Directory.Exists(directory)) { return 1; }
    if (File.Exists(directory)) { return 2; }
    string temporaryPath = CreateScopedTemporaryFile(directory);
    if (File.Exists(temporaryPath)) { return 43; }

    string originalDirectory = Directory.GetCurrentDirectory();
    Directory.SetCurrentDirectory(directory);
    if (Directory.GetCurrentDirectory() != directory) { return 28; }
    Directory.SetCurrentDirectory(originalDirectory);

    File.WriteAllText(first, "Aster System.IO");
    Directory.CreateDirectory(child);
    if (!File.Exists(first)) { return 3; }
    if (Directory.Exists(first)) { return 4; }
    if (File.ReadAllText(first) != "Aster System.IO") { return 5; }
    List<string> files = Directory.GetFiles(directory);
    List<string> directories = Directory.GetDirectories(directory);
    List<string> entries = Directory.GetFileSystemEntries(directory);
    if (files.Count != 1 || !files.Contains(first)) { return 25; }
    if (directories.Count != 1 || !directories.Contains(child)) { return 26; }
    if (entries.Count != 2 || !entries.Contains(first) ||
        !entries.Contains(child)) { return 27; }

    File.WriteAllText(second, "one\r\ntwo\rthree\n");
    List<string> mixedLines = File.ReadAllLines(second);
    if (mixedLines.Count != 3 || mixedLines[0] != "one" ||
        mixedLines[1] != "two" || mixedLines[2] != "three")
        { return 32; }

    List<string> lines = new();
    lines.Add("alpha");
    lines.Add("beta");
    File.WriteAllLines(second, lines);
    List<string> readLines = File.ReadAllLines(second);
    if (readLines.Count != 2 || readLines[0] != "alpha" ||
        readLines[1] != "beta") { return 29; }
    File.AppendAllText(second, "tail");
    if (File.ReadAllText(second) != "alpha\nbeta\ntail") { return 30; }
    File.AppendAllText(second, "\n");
    List<string> appended = new();
    appended.Add("gamma");
    File.AppendAllLines(second, appended);
    if (File.ReadAllText(second) != "alpha\nbeta\ntail\ngamma\n")
        { return 31; }
    File.Delete(second);

    File.Copy(first, second);
    if (File.ReadAllText(second) != "Aster System.IO") { return 6; }

    bool rejectedExistingDestination = false;
    try
    {
        File.Copy(first, second);
    }
    catch (Exception error)
    {
        rejectedExistingDestination = true;
    }
    if (!rejectedExistingDestination) { return 7; }

    File.WriteAllText(first, "replacement");
    File.Copy(first, second, true);
    if (File.ReadAllText(second) != "replacement") { return 8; }

    File.Delete(first);
    File.Move(second, first);
    if (!File.Exists(first) || File.Exists(second)) { return 9; }

    File.Delete(first);
    File.Delete(first);
    Directory.Delete(child);
    Directory.Delete(directory);
    if (Directory.Exists(directory)) { return 10; }
    return 0;
}
