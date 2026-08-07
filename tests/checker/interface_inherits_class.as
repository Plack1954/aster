private class Base
{
}

private interface IInvalid : Base
{
    long Value();
}

int main() { return 0; }
