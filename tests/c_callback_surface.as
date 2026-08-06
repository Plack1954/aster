delegate long Transform(long value);

private extern long NativeApply(long value, Transform callback);

private long AddTwo(long value)
{
    return value + 2;
}

int main()
{
    Transform callback = AddTwo;
    return NativeApply(40, callback) == 42 ? 0 : 1;
}
