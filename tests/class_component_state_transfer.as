using System;
using Aster.Html;

private class SeededState
{
    private string label;
    private int initial;

    public SeededState(string label, int initial)
    {
        this.label = label;
        this.initial = initial;
    }

    private int Increment(int count)
    {
        return this.initial + count;
    }

    public Html Render()
    {
        return <section>
            <strong>{this.label}</strong>
            <output name="count">{this.initial}</output>
            <button type="button" onclick=this.Increment>Increment</button>
        </section>;
    }
}

int main()
{
    Console.WriteLine(
        (<SeededState label="A&B" initial=9 />).ToHtmlString()
    );
    return 0;
}
