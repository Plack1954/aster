using Aster.Interop;

int main() {
    Result<string, string> value =
        NativeProcessEnvironment("ASTER_C_ENV_TEST");
    switch (value) {
        case Result.Ok(configured): {
            Console.WriteLine(configured);
        }
        case Result.Err(error): {
            Console.WriteLine(error);
            return 1;
        }
    }
    return 0;
}
