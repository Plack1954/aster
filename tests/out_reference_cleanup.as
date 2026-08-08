private extern NativeHandle NativeHandleOpenId(long id);
private extern long NativeHandleId(const ref NativeHandle handle);
private extern long NativeHandleDropLog();

private struct OutResource {
    NativeHandle handle;
}

private void Replace(out OutResource value) {
    value = OutResource {
        handle: NativeHandleOpenId(2),
    };
}

int main() {
    {
        OutResource resource = OutResource {
            handle: NativeHandleOpenId(1),
        };
        Replace(out resource);
        long replacedDropLog = NativeHandleDropLog();
        long replacementId = NativeHandleId(resource.handle);
        Console.WriteLine(replacedDropLog);
        Console.WriteLine(replacementId);
    }
    long finalDropLog = NativeHandleDropLog();
    Console.WriteLine(finalDropLog);
    return 0;
}
