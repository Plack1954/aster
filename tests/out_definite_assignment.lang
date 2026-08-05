private void SetOutput(out int value)
{
    value = 11;
}

private void ForwardOutput(out int value)
{
    SetOutput(out value);
}

private void SetByBranch(bool first, out int value)
{
    if (first) { value = 21; }
    else { value = 22; }
}

private void AssignThenThrow(out int value)
{
    value = 77;
    throw new IOException("output failure");
}

int main()
{
    int value = 5;
    ForwardOutput(out value);
    Console.WriteLine(value);
    SetByBranch(false, out value);
    Console.WriteLine(value);
    try { AssignThenThrow(out value); }
    catch (Exception error)
    {
        Console.WriteLine(error.Message);
        Console.WriteLine(value);
    }
    if (value != 77) { return 1; }
    return 0;
}
