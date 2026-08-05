using System.Text;

int main() {
    Console.WriteLine("aster.as".StartsWith("aster"));
    Console.WriteLine("aster.as".EndsWith(".as"));
    Console.WriteLine("typed Aster programs".Contains("Aster"));
    long minimum = -9223372036854775807 - 1;
    ulong maximum = 18446744073709551615;
    Console.WriteLine(minimum.ToString());
    Console.WriteLine(maximum.ToString());
    return 0;
}
