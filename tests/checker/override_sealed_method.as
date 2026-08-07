private class Base
{
    public virtual long Value() { return 1; }
}

private class Middle : Base
{
    public sealed override long Value() { return 2; }
}

private class Derived : Middle
{
    public override long Value() { return 3; }
}

int main() { return 0; }
