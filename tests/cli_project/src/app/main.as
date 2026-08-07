namespace App.Main;

using Aster.Interop;

int main() {
    nuint index = 0;
    while (index < NativeProcessArgCount()) {
        switch (NativeProcessArg(index)) {
            case Result.Ok(argument): {
                Console.WriteLine(argument);
            }
            case Result.Err(error): {
                Console.WriteLine(error);
                return 1;
            }
        }
        index = index + 1;
    }
    return 0;
}
