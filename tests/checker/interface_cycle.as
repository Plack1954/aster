interface ILeft : IRight
{
    long Left();
}

interface IRight : ILeft
{
    long Right();
}

int main() { return 0; }
