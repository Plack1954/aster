private struct Tracked
{
    int Id;
}

~Tracked()
{
    Console.WriteLine("cleaned");
}

private void Fail()
{
    Tracked value = new()
    {
        Id = 1
    };
    throw new IOException("database unavailable");
}

private void ThrowKnownException(int kind)
{
    if (kind == 0) { throw new FormatException("format"); }
    if (kind == 1) { throw new OverflowException("overflow"); }
    if (kind == 2) { throw new ArgumentException("argument"); }
    if (kind == 3)
    {
        throw new InvalidOperationException("invalid operation");
    }
    if (kind == 4) { throw new IOException("I/O"); }
    throw new JsonException("JSON");
}

int main()
{
    bool typedCatch = false;
    try
    {
        throw new IOException("typed");
    }
    catch (IOException error)
    {
        typedCatch = error.Message == "typed";
    }
    if (!typedCatch) { return 1; }

    bool wrongCatchRan = false;
    bool baseCatchRan = false;
    try
    {
        try
        {
            throw new IOException("skip mismatched handler");
        }
        catch (FormatException error)
        {
            wrongCatchRan = true;
        }
    }
    catch (Exception error)
    {
        baseCatchRan = true;
    }
    if (wrongCatchRan || !baseCatchRan) { return 2; }

    try
    {
        Fail();
        Console.WriteLine("unreachable");
    }
    catch (Exception error)
    {
        Console.WriteLine(error.Message);
    }

    Console.WriteLine("recovered");
    return 0;
}
