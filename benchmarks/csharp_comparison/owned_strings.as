using System.Text;

private string Record(long index, bool active)
{
    return $"customer-{index}:active={active}:balance={index * 3}";
}

int main()
{
    nuint total = 0;
    bool active = true;
    for (long i = 0; i < 500000; i++)
    {
        string value = Record(i, active);
        total += value.Length;
        active = !active;
    }
    Console.WriteLine(total);
    return 0;
}
