namespace BrowserCompare;

using Aster.Html;

public struct RowsPatch
{
    int nextId;
    Html rows;
}

public KeyedRemove RemoveRow(string key)
{
    return RemoveKey(key);
}

private Html Row(int id, bool updated)
{
    string key = $"row-{id}";
    string label = updated ? $"row {id} !!!" : $"row {id}";
    return <tr id=key>
        <td>{id}</td>
        <td>{label}</td>
        <td>
            <button
                type="button"
                name="key"
                value=key
                aria-controls="row-list"
                onclick=RemoveRow
            >Delete</button>
        </td>
    </tr>;
}

private RowsPatch MakeRows(int nextId, int count)
{
    List<Html> rows = new();
    for (int offset = 0; offset < count; offset++)
    {
        rows.Add(Row(nextId + offset, false));
    }
    return new()
    {
        nextId = nextId + count,
        rows = <>{rows}</>
    };
}

public RowsPatch Create1000(int nextId)
{
    return MakeRows(nextId, 1000);
}

public RowsPatch Append1000(int nextId)
{
    return MakeRows(nextId, 1000);
}

public Html UpdateEvery10th()
{
    List<Html> rows = new();
    for (int id = 0; id < 1000; id += 10)
    {
        rows.Add(Row(id, true));
    }
    return <>{rows}</>;
}

public KeyedSwap SwapRows()
{
    return SwapKeys("row-1", "row-998");
}

public KeyedClear ClearRows()
{
    return ClearKeys();
}

private Html Page()
{
    return <main id="benchmark">
        <h1>Aster retained DOM comparison</h1>
        <output name="nextId" hidden=true>0</output>
        <button
            type="button"
            name="createAction"
            aria-controls="row-list"
            onclick=Create1000
        >Create 1,000</button>
        <button
            type="button"
            name="appendAction"
            aria-controls="row-list"
            onclick=Append1000
        >Append 1,000</button>
        <button
            type="button"
            name="updateAction"
            aria-controls="row-list"
            onclick=UpdateEvery10th
        >Update every 10th</button>
        <button
            type="button"
            name="swapAction"
            aria-controls="row-list"
            onclick=SwapRows
        >Swap rows</button>
        <button
            type="button"
            name="clearAction"
            aria-controls="row-list"
            onclick=ClearRows
        >Clear</button>
        <table><tbody id="row-list"></tbody></table>
    </main>;
}

int main()
{
    Console.WriteLine(Page().ToHtmlString());
    return 0;
}
