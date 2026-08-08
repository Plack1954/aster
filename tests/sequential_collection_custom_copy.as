using System.Collections.Generic;

private struct SequenceCopyItem {
    long value;

    public SequenceCopyItem(const ref SequenceCopyItem other) {
        value = other.value + 1;
    }
}

int main() {
    SequenceCopyItem queueItem = new() { value = 10 };
    Queue<SequenceCopyItem> queueOriginal = new();
    queueOriginal.Enqueue(queueItem);
    Queue<SequenceCopyItem> queueCopy = copy(queueOriginal);
    SequenceCopyItem queueOriginalValue = copy(queueOriginal.Peek());
    SequenceCopyItem queueCopyValue = copy(queueCopy.Peek());
    Console.WriteLine(queueOriginalValue.value);
    Console.WriteLine(queueCopyValue.value);

    SequenceCopyItem stackItem = new() { value = 20 };
    Stack<SequenceCopyItem> stackOriginal = new();
    stackOriginal.Push(stackItem);
    Stack<SequenceCopyItem> stackCopy = copy(stackOriginal);
    SequenceCopyItem stackOriginalValue = copy(stackOriginal.Peek());
    SequenceCopyItem stackCopyValue = copy(stackCopy.Peek());
    Console.WriteLine(stackOriginalValue.value);
    Console.WriteLine(stackCopyValue.value);
    return 0;
}
