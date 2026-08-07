private union Value
{
    Number(long),
    Empty,
}

private long Read(Value value)
{
    switch (value)
    {
        case Value.Number(string number): { return 1; }
        case Value.Empty: { return 0; }
    }
}

int main()
{
    return 0;
}
