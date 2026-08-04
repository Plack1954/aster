using Aster.Testing;

int main() {
    TestSummary summary = TestSummary();
    summary = TestRecord(
        summary,
        "intentional failure",
        AssertEqI64(41, 42, "answer"),
    );
    return TestFinish(summary);
}
