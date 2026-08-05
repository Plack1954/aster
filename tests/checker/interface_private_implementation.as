interface IValue
{
    long Value();
}

class Hidden : IValue
{
    private long Value() { return 1; }
}

int main() { return 0; }
