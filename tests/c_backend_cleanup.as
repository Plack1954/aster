struct Tracked {
    long id;
}

~Tracked() {
    Console.WriteLine(self.id);
}

private Result<long, long> fail() {
    return Result.Err(-1);
}

private void ReverseScope() {
    Tracked first = Tracked { id: 1 };
    Tracked second = Tracked { id: 2 };
}

private long EarlyReturn() {
    Tracked value = Tracked { id: 3 };
    return 7;
}

private Result<long, long> CleanupThroughTry() {
    Tracked value = Tracked { id: 4 };
    long result = try fail();
    return Result.Ok(result);
}

private void CleanupThroughBreak() {
    while (true) {
        Tracked value = Tracked { id: 5 };
        break;
    }
}

private void consume(Tracked value) {
}

private void MovedValueCleanup() {
    Tracked value = Tracked { id: 6 };
    consume(value);
}

private string ThrowAfterFirstArgument() {
    throw new IOException("argument evaluation");
}

private void ConsumePair(Tracked first, string second) {
}

private void CleanupTemporaryOnExceptionTransfer() {
    try {
        ConsumePair(
            new Tracked { id = 7 },
            ThrowAfterFirstArgument()
        );
    }
    catch (IOException error) {
        Console.WriteLine(70);
    }
}

private void CleanupAcrossMismatchedCatch() {
    try {
        Tracked outer = new() { id = 8 };
        try {
            throw new IOException("mismatch");
        }
        catch (FormatException error) {
            Console.WriteLine(-1);
        }
    }
    catch (Exception error) {
        Console.WriteLine(80);
    }
}

private void CleanupCatchBeforeExceptionalFinally() {
    try {
        try {
            throw new IOException("caught");
        }
        catch (IOException error) {
            Tracked caught = new() { id = 9 };
            throw new FormatException(error.Message);
        }
        finally {
            Console.WriteLine(90);
        }
    }
    catch (FormatException error) {
        Console.WriteLine(91);
    }
}

private void CleanupMatchBindingOnNormalExit() {
    Result<Tracked, long> result = Result.Ok(
        new Tracked { id = 10 }
    );
    switch (result) {
        case Result.Ok(value): {
            Console.WriteLine(100);
        }
        case Result.Err(error): {
            Console.WriteLine(-1);
        }
    }
}

int main() {
    ReverseScope();
    long returned = EarlyReturn();
    Result<long, long> failed = CleanupThroughTry();
    CleanupThroughBreak();
    MovedValueCleanup();
    CleanupTemporaryOnExceptionTransfer();
    CleanupAcrossMismatchedCatch();
    CleanupCatchBeforeExceptionalFinally();
    CleanupMatchBindingOnNormalExit();
    return 0;
}
