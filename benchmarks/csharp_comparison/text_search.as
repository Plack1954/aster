using System.Text;

int main()
{
    string text = "Aster makes native web development fast and predictable";
    long matches = 0;
    for (int i = 0; i < 5000000; i++)
    {
        if (text.StartsWith("Aster")) { matches += 1; }
        if (text.EndsWith("predictable")) { matches += 1; }
        if (text.Contains("native web")) { matches += 1; }
        matches += (long)text.IndexOf("development");
    }
    Console.WriteLine(matches);
    return 0;
}
