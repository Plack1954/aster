using System.Collections.Generic;

struct ConditionalCopyItem {
    long value;

    public ConditionalCopyItem(const ref ConditionalCopyItem other) {
        value = other.value + 1;
    }
}

struct ConditionalCopyHolder {
    ConditionalCopyItem item;
}

int main() {
    ConditionalCopyItem queueSeed = new() { value = 10 };
    Queue<ConditionalCopyItem> queue = new();
    queue.Enqueue(queueSeed);
    ConditionalCopyItem queueResult = new() { value = 99 };
    if (!queue.TryPeek(out queueResult) || queueResult.value != 12) {
        return 1;
    }
    Console.WriteLine(queueResult.value);
    queue.Clear();
    if (queue.TryPeek(out queueResult) || queueResult.value != 0) {
        return 2;
    }
    Console.WriteLine(queueResult.value);

    ConditionalCopyItem stackSeed = new() { value = 20 };
    Stack<ConditionalCopyItem> stack = new();
    stack.Push(stackSeed);
    ConditionalCopyItem stackResult = new() { value = 99 };
    if (!stack.TryPeek(out stackResult) || stackResult.value != 22) {
        return 3;
    }
    Console.WriteLine(stackResult.value);
    stack.Clear();
    if (stack.TryPeek(out stackResult) || stackResult.value != 0) {
        return 4;
    }
    Console.WriteLine(stackResult.value);

    ConditionalCopyItem dictionarySeed = new() { value = 30 };
    Dictionary<int, ConditionalCopyItem> dictionary = new();
    dictionary.Add(7, dictionarySeed);
    ConditionalCopyHolder holder = new() {
        item = new() { value = 99 }
    };
    if (!dictionary.TryGetValue(7, out holder.item) ||
        holder.item.value != 32) {
        return 5;
    }
    Console.WriteLine(holder.item.value);
    if (dictionary.TryGetValue(8, out holder.item) ||
        holder.item.value != 0) {
        return 6;
    }
    Console.WriteLine(holder.item.value);
    return 0;
}
