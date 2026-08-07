namespace Nested.Main;

using Nested.NestedOwnershipDep;

private extern NativeHandle NativeHandleOpen();

private struct Helper {
    long id;
}

int main() {
    dep.Helper importedHelper =
        new dep.Helper {
            handle = NativeHandleOpen(),
        };
    Wrapper wrapper = Wrapper {
        helper: importedHelper,
    };
    Wrapper invalidCopy = wrapper;
    Console.WriteLine(invalidCopy.helper.handle);
    return 0;
}
