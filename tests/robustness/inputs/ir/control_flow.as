namespace Robustness.Ir;

private long Choose(bool condition, long left, long right) {
    if (condition) {
        return left;
    }
    return right;
}

int main() {
    return Choose(true, 1, 2);
}
