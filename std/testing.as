namespace Aster.Testing;

using System.Text;

public using TestResult = Result<bool, string>;

public struct TestSummary {
    nuint passed;
    nuint failed;
}

public TestResult TestPass() {
    return Result.Ok(true);
}

public TestResult TestFail(string message) {
    return Result.Err(message);
}

public TestResult AssertTrue(bool value, string message) {
    if (value) {
        return TestPass();
    }
    return TestFail(message);
}

public TestResult AssertFalse(bool value, string message) {
    if (!value) {
        return TestPass();
    }
    return TestFail(message);
}

public TestResult AssertEqStr(
    string actual,
    string expected,
    string context
) {
    if (actual == expected) {
        return TestPass();
    }
    StringBuilder message = new();
    message.Append(context);
    message.Append(": expected `");
    message.Append(expected);
    message.Append("`, found `");
    message.Append(actual);
    message.Append("`");
    return Result.Err(message.ToString());
}

public TestResult AssertContains(
    string actual,
    string expectedPart,
    string context
) {
    if (actual.Contains(expectedPart)) {
        return TestPass();
    }
    StringBuilder message = new();
    message.Append(context);
    message.Append(": expected `");
    message.Append(actual);
    message.Append("` to contain `");
    message.Append(expectedPart);
    message.Append("`");
    return Result.Err(message.ToString());
}

public TestResult AssertEqI64(
    long actual,
    long expected,
    string context
) {
    if (actual == expected) {
        return TestPass();
    }
    string expectedText = expected.ToString();
    string actualText = actual.ToString();
    StringBuilder message = new();
    message.Append(context);
    message.Append(": expected ");
    message.Append(expectedText);
    message.Append(", found ");
    message.Append(actualText);
    return Result.Err(message.ToString());
}

public TestResult AssertEqU64(
    ulong actual,
    ulong expected,
    string context
) {
    if (actual == expected) {
        return TestPass();
    }
    string expectedText = expected.ToString();
    string actualText = actual.ToString();
    StringBuilder message = new();
    message.Append(context);
    message.Append(": expected ");
    message.Append(expectedText);
    message.Append(", found ");
    message.Append(actualText);
    return Result.Err(message.ToString());
}

public TestSummary TestSummary() {
    return new TestSummary {
        passed = 0,
        failed = 0,
    };
}

public TestSummary TestRecord(
    TestSummary summary,
    string name,
    TestResult result
) {
    TestSummary output = summary;
    switch (result) {
        case Result.Ok(value): {
            output.passed = output.passed + 1;
            Console.WriteLine("[pass]");
            Console.WriteLine(name);
        }
        case Result.Err(error): {
            output.failed = output.failed + 1;
            Console.WriteLine("[FAIL]");
            Console.WriteLine(name);
            Console.WriteLine(error);
        }
    }
    return output;
}

public int TestFinish(TestSummary summary) {
    Console.WriteLine("test summary");
    Console.WriteLine(summary.passed);
    Console.WriteLine(summary.failed);
    if (summary.failed > 0) {
        return 1;
    }
    return 0;
}
