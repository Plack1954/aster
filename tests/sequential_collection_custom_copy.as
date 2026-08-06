using System.Collections.Generic;

struct SequenceCopyItem {
    long value;

    public SequenceCopyItem(const ref SequenceCopyItem other) {
        value = other.value + 1;
    }
}

int main() {
    SequenceCopyItem queueItem = new() { value = 10 };
    Queue<SequenceCopyItem> queueOriginal = new();
    queueOriginal.Enqueue(queueItem);
    Queue<SequenceCopyItem> queueCopy = queueOriginal;
    SequenceCopyItem queueOriginalValue = queueOriginal.Peek();
    SequenceCopyItem queueCopyValue = queueCopy.Peek();
    Console.WriteLine(queueOriginalValue.value);
    Console.WriteLine(queueCopyValue.value);

    SequenceCopyItem stackItem = new() { value = 20 };
    Stack<SequenceCopyItem> stackOriginal = new();
    stackOriginal.Push(stackItem);
    Stack<SequenceCopyItem> stackCopy = stackOriginal;
    SequenceCopyItem stackOriginalValue = stackOriginal.Peek();
    SequenceCopyItem stackCopyValue = stackCopy.Peek();
    Console.WriteLine(stackOriginalValue.value);
    Console.WriteLine(stackCopyValue.value);
    return 0;
}
