class Base
{
    public long Value() { return 1; }
}

class Derived : Base
{
    public override long Value() { return 2; }
}

int main() { return 0; }
