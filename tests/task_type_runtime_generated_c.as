union DeferredValue
{
    Ready(int),
    Pending(Task<int>),
}

private DeferredValue Identity(DeferredValue value)
{
    return value;
}

int main()
{
    DeferredValue value = Identity(DeferredValue.Ready(42));
    switch (value)
    {
        case DeferredValue.Ready(number): {
            Console.WriteLine(number);
            return 0;
        }
        case DeferredValue.Pending(task): { return 1; }
    }
}
