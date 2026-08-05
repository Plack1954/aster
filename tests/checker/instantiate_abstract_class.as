abstract class Base
{
    public Base() { }
    public abstract long Value();
}

int main()
{
    Base value = new Base();
    delete value;
    return 0;
}
