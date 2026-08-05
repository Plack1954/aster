namespace Tests.BrowserSmoke;

using Lime.Browser;
using Aster.Html;
using System.Text;

int main()
{
    switch (BrowserAssets("build/browser", "/browser", "site"))
    {
        case Result.Err(error): { return 1; }
        case Result.Ok(assets): {
            string rendered = assets.loader().ToHtmlString();
            if (!rendered.Contains(
                "<script type=\"module\" src=\"/browser/site.js\"></script>"
            ))
            {
                return 1;
            }
        }
    }
    switch (BrowserAssets("build/browser", "/browser/", "site"))
    {
        case Result.Ok(assets): { return 1; }
        case Result.Err(error): {
        }
    }
    switch (BrowserAssets("build/browser", "/browser", "../site"))
    {
        case Result.Ok(assets): { return 1; }
        case Result.Err(error): { return 0; }
    }
}
