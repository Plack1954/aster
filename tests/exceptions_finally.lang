private void Fail(string message)
{
    throw new Exception(message);
}

private void PropagateThroughFinally()
{
    try
    {
        Fail("propagated");
    }
    finally
    {
        Console.WriteLine("finally-propagate");
    }
}

private int ReturnThroughFinally()
{
    try
    {
        return 7;
    }
    finally
    {
        Console.WriteLine("finally-return");
    }
}

private void ReplaceExceptionInFinally()
{
    try
    {
        Fail("original");
    }
    finally
    {
        throw new Exception("replacement");
    }
}

private void RethrowCurrentException()
{
    try
    {
        Fail("rethrown");
    }
    catch (Exception error)
    {
        throw;
    }
}

int main()
{
    try
    {
        Console.WriteLine("normal");
    }
    finally
    {
        Console.WriteLine("finally-normal");
    }

    try
    {
        Fail("caught");
    }
    catch (Exception error)
    {
        Console.WriteLine(error.Message);
    }
    finally
    {
        Console.WriteLine("finally-caught");
    }

    try
    {
        PropagateThroughFinally();
    }
    catch (Exception error)
    {
        Console.WriteLine(error.Message);
    }

    try
    {
        ReplaceExceptionInFinally();
    }
    catch (Exception error)
    {
        Console.WriteLine(error.Message);
    }

    try
    {
        RethrowCurrentException();
    }
    catch (Exception error)
    {
        Console.WriteLine(error.Message);
    }

    int value = ReturnThroughFinally();
    Console.WriteLine(value);

    while (true)
    {
        try
        {
            break;
        }
        finally
        {
            Console.WriteLine("finally-break");
        }
    }

    return 0;
}
