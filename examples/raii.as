private extern NativeHandle NativeHandleOpenId(long id);
private extern long NativeHandleDropLog();

private Result<long, string> AlwaysError() {
    return Result.Err("failure");
}

private long EarlyReturn() {
    NativeHandle handle = NativeHandleOpenId(3);
    return 7;
}

private Result<long, string> CleanupThroughTry() {
    NativeHandle handle = NativeHandleOpenId(4);
    long value = try AlwaysError();
    return Result.Ok(value);
}

int main() {
    {
        NativeHandle first = NativeHandleOpenId(1);
        NativeHandle second = NativeHandleOpenId(2);
    }

    // Reverse declaration order: 2, then 1.
    Console.WriteLine(NativeHandleDropLog());

    long value = EarlyReturn();
    Result<long, string> failed = CleanupThroughTry();

    // Early return adds 3; try propagation adds 4.
    Console.WriteLine(NativeHandleDropLog());
    return 0;
}
