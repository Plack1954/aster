namespace BrowserCompare;

using Aster.Html;

private extern Task Task.Delay(int milliseconds);

private class AsyncProbe
{
    private string status;

    public AsyncProbe()
    {
        this.status = "idle";
    }

    private async Task CompleteSlow()
    {
        await Task.Delay(25);
        this.status = "slow";
    }

    private async Task CompleteFast()
    {
        await Task.Delay(1);
        this.status = "fast";
    }

    public Html Render()
    {
        return <section id="async-probe">
            <output name="asyncStatus">{this.status}</output>
            <button type="button" name="asyncSlow" onclick=this.CompleteSlow>Slow async</button>
            <button type="button" name="asyncFast" onclick=this.CompleteFast>Fast async</button>
        </section>;
    }
}

private struct BenchmarkRow
{
    string key;
    int id;
    string label;
}

private class BenchmarkTable
{
    private List<BenchmarkRow> rows;
    private int nextId;

    public BenchmarkTable()
    {
        rows = new();
        nextId = 0;
    }

    private void AddRows(int count)
    {
        for (int offset = 0; offset < count; offset++)
        {
            int id = this.nextId + offset;
            this.rows.Add(new()
            {
                key = $"row-{id}",
                id = id,
                label = $"row {id}"
            });
        }
        this.nextId += count;
    }

    private void Create1000()
    {
        this.rows.Clear();
        this.nextId = 0;
        this.AddRows(1000);
    }

    private void Append1000()
    {
        this.AddRows(1000);
    }

    private void UpdateEvery10th()
    {
        for (nuint index = 0; index < this.rows.Count; index += 10)
        {
            BenchmarkRow row = this.rows[index];
            row.label = $"row {row.id} !!!";
            this.rows.Set(index, row);
        }
    }

    private void SwapRows()
    {
        BenchmarkRow first = this.rows[1];
        BenchmarkRow second = this.rows[998];
        this.rows.Set(1, second);
        this.rows.Set(998, first);
    }

    private void DeleteRow(string key)
    {
        for (nuint index = 0; index < this.rows.Count; index++)
        {
            if (this.rows[index].key == key)
            {
                this.rows.RemoveAt(index);
                return;
            }
        }
    }

    private void ClearRows()
    {
        this.rows.Clear();
    }

    private Html Rows()
    {
        List<Html> rendered = new();
        foreach (BenchmarkRow row in this.rows)
        {
            rendered.Add(<tr key=row.key id=row.key>
                <td>{row.id}</td>
                <td>{row.label}</td>
                <td><button type="button" name="key" value=row.key aria-controls="row-list" onclick=this.DeleteRow>Delete</button></td>
            </tr>);
        }
        return <>{rendered}</>;
    }

    public Html Render()
    {
        return <main id="benchmark">
            <h1>Aster retained DOM comparison</h1>
            <button type="button" name="createAction" aria-controls="row-list" onclick=this.Create1000>Create 1,000</button>
            <button type="button" name="appendAction" aria-controls="row-list" onclick=this.Append1000>Append 1,000</button>
            <button type="button" name="updateAction" aria-controls="row-list" onclick=this.UpdateEvery10th>Update every 10th</button>
            <button type="button" name="swapAction" aria-controls="row-list" onclick=this.SwapRows>Swap rows</button>
            <button type="button" name="clearAction" aria-controls="row-list" onclick=this.ClearRows>Clear</button>
            <table><tbody id="row-list">{this.Rows()}</tbody></table>
            <AsyncProbe />
        </main>;
    }
}

int main()
{
    Console.WriteLine((<BenchmarkTable />).ToHtmlString());
    return 0;
}
