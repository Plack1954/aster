private Result<long, string> MaybeNumber(bool succeed) {
    if (succeed) {
        return Result.Ok(42);
    }
    return Result.Err("not available");
}

private Result<long, string> SuccessfulPath() {
    Buffer scratch = Buffer.allocate(32);
    long number = try MaybeNumber(true);
    Console.WriteLine(number);
    return Result.Ok(number);
}

private Result<long, string> FailingPath() {
    Buffer scratch = Buffer.allocate(64);
    long number = try MaybeNumber(false);
    return Result.Ok(number);
}

int main() {
    Result<long, string> success = SuccessfulPath();
    Result<long, string> failure = FailingPath();
    return 0;
}
