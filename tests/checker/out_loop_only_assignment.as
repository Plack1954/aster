private void InvalidLoop(bool repeat, out int value)
{
    while (repeat)
    {
        value = 1;
        repeat = false;
    }
}

int main()
{
    int value = 0;
    InvalidLoop(true, out value);
    return 0;
}
