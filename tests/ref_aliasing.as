private void ObserveAliases(ref int first, ref int second)
{
    first = 7;
    Console.WriteLine(second);
    second = 8;
    Console.WriteLine(first);
}

private void SetNested(ref int value)
{
    value = 19;
}

private void ForwardNested(ref int value)
{
    SetNested(value);
    Console.WriteLine(value);
}

private void MutateThenThrow(ref int value)
{
    value = 29;
    throw new IOException("alias failure");
}

private void ReplaceManaged(ref string first, ref string second)
{
    first = "changed";
    Console.WriteLine(second);
}

int main()
{
    int scalar = 1;
    ObserveAliases(scalar, scalar);
    if (scalar != 8) { return 1; }

    ForwardNested(scalar);
    if (scalar != 19) { return 2; }

    try
    {
        MutateThenThrow(scalar);
    }
    catch (Exception error)
    {
        Console.WriteLine(error.Message);
    }
    if (scalar != 29) { return 3; }

    string text = "before";
    ReplaceManaged(text, text);
    if (text != "changed") { return 4; }

    Box box = new() { Value = 3 };
    ObserveAliases(box.Value, box.Value);
    if (box.Value != 8) { return 5; }
    return 0;
}
private struct Box
{
    int Value;
}
