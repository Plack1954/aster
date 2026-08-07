#!/usr/bin/env bash
set -euo pipefail

lang=$1
scratch_root=$2

mkdir -p "$scratch_root"
site_directory=$(mktemp -d "$scratch_root/aster-web-ssg.XXXXXX")
cleanup() {
    rm -rf -- "$site_directory"
}
trap cleanup EXIT

"$lang" project build-site \
    packages/aster_web/aster.toml "$site_directory" ssg_smoke

test -f "$site_directory/index.html"
test -f "$site_directory/about/index.html"
test -f "$site_directory/articles/aster/index.html"
test -f "$site_directory/robots.txt"
test -f "$site_directory/404.html"
test -f "$site_directory/assets/site.css"
test -f "$site_directory/assets/icons/mark.svg"
test "$(find "$site_directory" -type f | wc -l)" -eq 7

grep -q '<h1>Home</h1>' "$site_directory/index.html"
grep -q '<h1>About</h1>' "$site_directory/about/index.html"
grep -q '<h1>aster</h1>' \
    "$site_directory/articles/aster/index.html"
grep -q 'User-agent: \*' "$site_directory/robots.txt"
grep -q '<h1>Not found</h1>' "$site_directory/404.html"
cmp packages/aster_web/test_assets/site.css "$site_directory/assets/site.css"
cmp packages/aster_web/test_assets/icons/mark.svg \
    "$site_directory/assets/icons/mark.svg"

async_directory="$site_directory/async"
"$lang" project build-site \
    packages/aster_web/aster.toml "$async_directory" async_ssg_smoke
test -f "$async_directory/index.html"
test -f "$async_directory/404.html"
grep -q '<h1>Async static page</h1>' "$async_directory/index.html"

blog_directory="$site_directory/blog"
"$lang" project build-site \
    packages/aster_web/aster.toml "$blog_directory/" blog_fixture

test -f "$blog_directory/index.html"
test -f "$blog_directory/blog/index.html"
test -f "$blog_directory/blog/first-post/index.html"
test -f "$blog_directory/about/index.html"
test -f "$blog_directory/feed.xml"
test -f "$blog_directory/robots.txt"
test -f "$blog_directory/404.html"
test "$(find "$blog_directory" -type f | wc -l)" -eq 7
grep -q '<h1>Blog</h1>' "$blog_directory/index.html"
grep -q '<h1>First post</h1>' \
    "$blog_directory/blog/first-post/index.html"
grep -q '<rss version="2.0">' "$blog_directory/feed.xml"
