private void InvalidEarlyReturn(bool stop, out int value)
{
    if (stop) { return; }
    value = 1;
}

int main()
{
    int value = 0;
    InvalidEarlyReturn(true, out value);
    return 0;
}
