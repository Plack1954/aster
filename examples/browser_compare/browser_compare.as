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

private Html Row(int id)
{
    string key = $"row-{id}";
    return <tr id=key>
        <td>{id}</td>
        <td>row {id}</td>
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
        rows.Add(Row(nextId + offset));
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
        <table><tbody id="row-list"></tbody></table>
    </main>;
}

int main()
{
    Console.WriteLine(Page().ToHtmlString());
    return 0;
}
