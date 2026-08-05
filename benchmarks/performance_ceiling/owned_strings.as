using System.Text;

private string Record(long index, bool active)
{
    return $"customer-{index}:active={active}:balance={index * 3}";
}

int main()
{
    nuint total = 0;
    long index = 0;
    bool active = true;
    while (index < 500000)
    {
        string value = Record(index, active);
        total = total + value.Length;
        active = !active;
        index = index + 1;
    }
    Console.WriteLine(total);
    return 0;
}
