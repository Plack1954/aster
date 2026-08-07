private interface IValue
{
    long Value();
}

private class Hidden : IValue
{
    private long Value() { return 1; }
}

int main() { return 0; }
