private extern NativeHandle NativeHandleOpenId(long id);
private extern long NativeHandleDropLog();

private struct Tracked {
    long id;
    NativeHandle handle;
}

~Tracked() {
    Console.WriteLine(self.id);
}

private Result<long, string> fail() {
    return Result.Err("failed");
}

private long EarlyReturn() {
    NativeHandle handle = NativeHandleOpenId(3);
    Tracked tracked = new() {
        id = 3,
        handle = handle,
    };
    return 7;
}

private Result<long, string> CleanupThroughTry() {
    NativeHandle handle = NativeHandleOpenId(4);
    Tracked tracked = new() {
        id = 4,
        handle = handle,
    };
    long value = try fail();
    return Result.Ok(value);
}

int main() {
    {
        NativeHandle firstHandle = NativeHandleOpenId(1);
        Tracked first = new() {
            id = 1,
            handle = firstHandle,
        };
        NativeHandle secondHandle = NativeHandleOpenId(2);
        Tracked second = new() {
            id = 2,
            handle = secondHandle,
        };
    }
    Console.WriteLine(NativeHandleDropLog());

    long value = EarlyReturn();
    Console.WriteLine(NativeHandleDropLog());

    Result<long, string> failed = CleanupThroughTry();
    Console.WriteLine(NativeHandleDropLog());
    return 0;
}
