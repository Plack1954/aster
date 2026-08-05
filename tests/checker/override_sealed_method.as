class Base
{
    public virtual long Value() { return 1; }
}

class Middle : Base
{
    public sealed override long Value() { return 2; }
}

class Derived : Middle
{
    public override long Value() { return 3; }
}

int main() { return 0; }
