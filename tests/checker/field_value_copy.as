using System.Text;

private struct Record {
    string name;
}

private string ViewName(string value) {
    return value;
}

int main() {
    Record record = Record {
        name: "borrowed field",
    };
    Console.WriteLine(ViewName(record.name));
    return 0;
}
