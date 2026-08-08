namespace Robustness.Parser;

struct Pair {
    long left;
    string right;
}

int main() {
    Pair value = new() { left = 1, right = "seed" };
    return value.left;
}
