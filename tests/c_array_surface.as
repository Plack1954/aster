private struct Samples {
    int Values[3];
}

private int sum(int values[3]) {
    int total = 0;
    foreach (int value in values) {
        total += value;
    }
    return total;
}

int main() {
    int values[3] = [7, 11, 13];
    Console.WriteLine(sum(values));

    int matrix[2][2] = [[1, 2], [3, 4]];
    Console.WriteLine(matrix[1][0]);

    Samples samples = new() {
        Values = [2, 4, 8],
    };
    Console.WriteLine(samples.Values[2]);
    return 0;
}
