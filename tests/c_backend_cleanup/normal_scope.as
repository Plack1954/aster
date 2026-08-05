int main()
{
    {
        string value = "normal-scope";
        if (value.Length == 0) { return 1; }
    }
    return 0;
}
