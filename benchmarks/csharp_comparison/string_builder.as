using System.Text;

int main()
{
    StringBuilder builder = new();
    for (int i = 0; i < 1000000; i++)
    {
        builder.Append(i % 10);
    }
    string value = builder.ToString();
    Console.WriteLine(value.Length);
    return 0;
}
