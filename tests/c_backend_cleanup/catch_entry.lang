int main()
{
    bool caught = false;
    try
    {
        throw new IOException("catch-entry");
    }
    catch (IOException error)
    {
        caught = error.Message == "catch-entry";
    }
    if (!caught) { return 1; }
    return 0;
}
