private interface IValue
{
    long Value();
}

private class Missing : IValue
{
}

int main() { return 0; }
