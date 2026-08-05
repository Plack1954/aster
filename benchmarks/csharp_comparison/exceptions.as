private void Fail(int value)
{
    throw new InvalidOperationException("expected failure");
}

int main()
{
    long caught = 0;
    for (int i = 0; i < 100000; i++)
    {
        try { Fail(i); }
        catch (InvalidOperationException error)
        {
            caught += (long)error.Message.Length;
        }
    }
    Console.WriteLine(caught);
    return 0;
}
