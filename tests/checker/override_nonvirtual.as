private class Base
{
    public long Value() { return 1; }
}

private class Derived : Base
{
    public override long Value() { return 2; }
}

int main() { return 0; }
