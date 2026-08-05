namespace System.Threading.Tasks;

// Task follows the .NET public vocabulary. Delay is implemented by each
// Aster executor; already-completed tasks need no runtime-specific native.
public extern Task Task.Delay(int milliseconds);
public extern Task Task.Delay(
    int milliseconds,
    CancellationToken cancellationToken
);

// The compiler provides the generic .NET-shaped combinators directly:
// Task.WhenAll(List<Task<T>>) -> Task<List<T>>
// Task.WhenAll(List<Task>)    -> Task
// Task.WhenAny(List<Task<T>>) -> Task<Task<T>>

public async Task<T> Task.FromResult<T>(T result)
{
    return result;
}

// Aster does not have static properties yet, so the .NET CompletedTask
// property is temporarily callable. The name and completion semantics match.
public async Task Task.CompletedTask()
{
    return;
}
