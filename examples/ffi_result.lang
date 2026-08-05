private extern Result<long, string> NativeCheckedValue(bool ok);

private Result<long, string> checked() {
    long value = try NativeCheckedValue(true);
    return Result.Ok(value);
}

int main() {
    Result<long, string> result = checked();
    switch (result) {
        case Result.Ok(value): {
            Console.WriteLine(value);
        }
        case Result.Err(error): {
            Console.WriteLine(error);
        }
    }
    return 0;
}
