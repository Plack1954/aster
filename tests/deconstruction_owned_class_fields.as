private class OwnedNode {
    public long value;

    public OwnedNode(long initial) {
        value = initial;
    }
}

private struct OwnedPair {
    OwnedNode first;
    OwnedNode second;
}

~OwnedPair() {
    delete self.first;
    delete self.second;
}

int main() {
    OwnedPair pair = new() {
        first = new OwnedNode(41),
        second = new OwnedNode(42)
    };
    (OwnedNode first, OwnedNode second) = pair;
    Console.WriteLine(first.value);
    Console.WriteLine(second.value);
    delete first;
    delete second;
    return 0;
}
