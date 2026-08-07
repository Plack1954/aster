private interface ILeft : IRight
{
    long Left();
}

private interface IRight : ILeft
{
    long Right();
}

int main() { return 0; }
