int main()
{
    bool caught = false;
    try
    {
        string value = "exception-transfer";
        if (value.Length == 0) { return 1; }
        try
        {
            throw new IOException("transfer");
        }
        catch (FormatException error)
        {
            return 2;
        }
    }
    catch (Exception error)
    {
        caught = true;
    }
    if (!caught) { return 3; }
    return 0;
}
