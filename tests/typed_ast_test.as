private long increment(long value) {
    long offset = 1;
    {
        long offset = 2;
        Console.WriteLine(offset);
    }
    return value + offset;
}

private Result<long, long> CleanupPlan(bool flag) {
    Buffer buffer = Buffer.allocate(8);
    if (flag) {
        return Result.Err(1);
    }
    long value = try Result.Ok(42);
    return Result.Ok(value);
}

private long UnsafeIndexIntent() {
    long values[1] = [7];
    unsafe {
        return values[0];
    }
}

int main() {
    Console.WriteLine(increment(41));
    return 0;
}
