using System.Text;

int main()
{
    char greekLower = (char)955;
    char greekUpper = (char)923;
    char arabicDigit = (char)1637;
    char emSpace = (char)8195;

    if (char.ToUpperInvariant(greekLower) != greekUpper ||
        char.ToLowerInvariant(greekUpper) != greekLower ||
        !char.IsLetter(greekLower) || !char.IsUpper(greekUpper) ||
        !char.IsLower(greekLower) || !char.IsDigit(arabicDigit) ||
        !char.IsWhiteSpace(emSpace) ||
        !char.IsLetterOrDigit(arabicDigit)) { return 1; }

    string source = "Straße Καλημέρα 世界 😀";
    List<char> scalars = source.ToCharArray();
    if (scalars.Count != 20 || scalars[0] != (char)83 ||
        scalars[19] != (char)128512) { return 2; }
    int iterated = 0;
    char finalScalar = (char)0;
    foreach (char scalar in source)
    {
        iterated += 1;
        finalScalar = scalar;
    }
    if (iterated != 20 || finalScalar != (char)128512) { return 7; }
    StringBuilder scalarBuilder = new();
    scalarBuilder.Append((char)955);
    scalarBuilder.Append((char)128512);
    if (scalarBuilder.ToString() != "λ😀") { return 8; }

    if ("Καλημέρα".ToUpperInvariant() != "ΚΑΛΗΜΈΡΑ" ||
        "Straße".ToUpperInvariant() != "STRASSE" ||
        "WORLD ÅNGSTRÖM".ToLowerInvariant() != "world ångström")
        { return 3; }

    UTF8Encoding utf8 = Encoding.UTF8();
    List<byte> bytes = utf8.GetBytes("Aλ😀");
    if (bytes.Count != 7 || bytes[0] != 65 || bytes[1] != 206 ||
        utf8.GetString(bytes) != "Aλ😀") { return 4; }

    List<byte> invalid = new();
    invalid.Add(240);
    invalid.Add(40);
    invalid.Add(140);
    invalid.Add(188);
    try
    {
        utf8.GetString(invalid);
        return 5;
    }
    catch (FormatException error)
    {
        if (error.Message.Length == 0) { return 6; }
    }

    return 0;
}
