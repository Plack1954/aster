// Prototype of the registry that file-based tooling would generate.
namespace Pages.Generated;

using Aster.Web.HttpApp;
using Index = Pages.Index;
using About = Pages.About;

public void AddPages(ref Router router) {
    RouterGetMut(router, "/", Index.page);
    RouterGetMut(router, "/about/", About.page);
}
