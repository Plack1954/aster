private bool TryDouble(int input, out int value)
{
    if (input < 0)
    {
        value = 0;
        return false;
    }
    value = input * 2;
    return true;
}

int main()
{
    int value = -1;
    if (!TryDouble(21, out value)) { return 1; }
    if (value != 42) { return 2; }
    if (TryDouble(-1, out value)) { return 3; }
    if (value != 0) { return 4; }
    return 0;
}
