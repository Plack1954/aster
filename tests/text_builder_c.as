using System.Text;

int main()
{
    string value = "Aster language";
    if (value.Length != 14) { return 1; }
    if (!value.StartsWith("Aster")) { return 2; }
    if (!value.EndsWith("language")) { return 3; }
    if (!value.Contains("er la")) { return 4; }
    if (value.IndexOf("language") != 6) { return 5; }
    if (value.IndexOf("a", 9) != 11) { return 6; }
    if (value.LastIndexOf("a") != 11) { return 7; }
    if (!value.StartsWith("") || !value.EndsWith("")) { return 40; }
    if (value.StartsWith("Aster language!") ||
        value.EndsWith("Aster language!")) { return 41; }
    if (value.Contains("missing") || value.IndexOf("missing") != -1)
        { return 42; }
    if (value.IndexOf("", value.Length) != (long)value.Length)
        { return 43; }
    if (value.IndexOf("", value.Length + 1) != -1) { return 44; }
    if ("aaaaab".IndexOf("aaab") != 2) { return 45; }
    if (value.Substring(6) != "language") { return 8; }
    if (value.Substring(0, 5) != "Aster") { return 9; }
    if (value.Insert(5, " programming") !=
        "Aster programming language") { return 10; }
    if (value.Remove(5) != "Aster") { return 11; }
    if (value.Remove(5, 9) != "Aster") { return 12; }
    if (value.Replace("a", "A") != "Aster lAnguAge") { return 13; }
    string? missing = null;
    string? empty = string.Empty;
    if (!string.IsNullOrEmpty(missing) ||
        !string.IsNullOrEmpty(empty) ||
        string.IsNullOrEmpty(value)) { return 14; }
    if (string.Concat("As", "te", "r") != "Aster") { return 15; }

    List<string> names = new();
    names.Add("Aster");
    names.Add("Pear");
    if (string.Join(", ", names) != "Aster, Pear") { return 16; }
    if (string.CompareOrdinal("Aster", "Aster") != 0) { return 17; }
    if (string.CompareOrdinal("Aster", "Pear") >= 0) { return 18; }
    if (!string.Equals("Aster", "Aster")) { return 19; }
    if (" \t\nAster\r ".Trim() != "Aster") { return 25; }
    if (" \tAster ".TrimStart() != "Aster ") { return 26; }
    if (" Aster\r\n".TrimEnd() != " Aster") { return 27; }
    string? whiteSpace = " \t\r\n";
    if (!string.IsNullOrWhiteSpace(missing) ||
        !string.IsNullOrWhiteSpace(whiteSpace) ||
        string.IsNullOrWhiteSpace(value)) { return 28; }
    if ("".Trim() != "" || "Aster".Trim() != "Aster") { return 29; }
    if ("  Aster 　".Trim() != "Aster")
        { return 30; }
    string? unicodeWhiteSpace = "   ";
    if (!string.IsNullOrWhiteSpace(unicodeWhiteSpace)) { return 31; }

    List<string> commaParts = "Aster,,Pear,".Split(",");
    if (commaParts.Count != 4 ||
        commaParts[0] != "Aster" ||
        commaParts[1] != "" ||
        commaParts[2] != "Pear" ||
        commaParts[3] != "") { return 32; }
    List<string> whole = "Aster".Split("");
    if (whole.Count != 1 || whole[0] != "Aster") { return 33; }
    List<string> words = "Aster  Pear Web".Split();
    if (words.Count != 4 ||
        words[0] != "Aster" ||
        words[1] != "" ||
        words[2] != "Pear" ||
        words[3] != "Web") { return 34; }
    List<string> emptyParts = "".Split(",");
    if (emptyParts.Count != 1 || emptyParts[0] != "") { return 35; }
    StringSplitOptions splitOptions =
        StringSplitOptions.RemoveEmptyEntries |
        StringSplitOptions.TrimEntries;
    List<string> cleanParts =
        " Aster, , Pear ,, Web ".Split(",", splitOptions);
    if (cleanParts.Count != 3 ||
        cleanParts[0] != "Aster" ||
        cleanParts[1] != "Pear" ||
        cleanParts[2] != "Web") { return 36; }
    List<string> limitedParts =
        "Aster,Pear,Web".Split(",", 2, StringSplitOptions.None);
    if (limitedParts.Count != 2 ||
        limitedParts[0] != "Aster" ||
        limitedParts[1] != "Pear,Web") { return 37; }
    List<string> noParts =
        "Aster".Split(",", 0, StringSplitOptions.None);
    if (noParts.Count != 0) { return 38; }
    splitOptions &= ~StringSplitOptions.TrimEntries;
    if (splitOptions != StringSplitOptions.RemoveEmptyEntries) { return 39; }

    StringBuilder builder = new();
    builder.Append("Aster");
    builder.Append(42);
    if (builder.Length != 7) { return 20; }
    string first = builder.ToString();
    builder.AppendLine();
    if (first != "Aster42") { return 21; }
    if (builder.ToString() != "Aster42\n") { return 22; }
    builder.Clear();
    if (builder.Length != 0) { return 23; }
    builder.AppendLine("Pear");
    if (builder.ToString() != "Pear\n") { return 24; }
    return 0;
}
