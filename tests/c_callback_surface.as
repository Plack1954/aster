delegate long Transform(long value);
delegate bool Predicate(bool value);

private extern long NativeApply(long value, Transform callback);
private extern long NativeIncrement(long value);
private extern long NativeSelect(long value);
private extern bool NativeSelect(bool value);

private long AddTwo(long value)
{
    return value + 2;
}

int main()
{
    Transform callback = AddTwo;
    Transform external = NativeIncrement;
    Transform selectNumber = NativeSelect;
    Predicate selectBoolean = NativeSelect;
    if (NativeApply(40, callback) != 42) { return 1; }
    if (external(41) != 42) { return 2; }
    if (selectNumber(42) != 42) { return 3; }
    return selectBoolean(true) ? 0 : 4;
}
