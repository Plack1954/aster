namespace Tests.Smoke;

using Lib.Math;

int main() {
    if (Lib.Math.answer() == 42) {
        Console.WriteLine("project test");
        return 0;
    }
    return 1;
}
