namespace Aster.Html;

public Html doctype() {
    return Html.UnsafeRaw("<!doctype html>");
}

// A typed browser transition requesting removal of one child from the
// collection identified by the event source's aria-controls relationship.
public struct KeyedRemove
{
    string key;
}

public KeyedRemove RemoveKey(string key)
{
    return new() { key = key };
}

public struct KeyedClear
{
    bool clear;
}

public KeyedClear ClearKeys()
{
    return new() { clear = true };
}

public struct KeyedSwap
{
    string first;
    string second;
}

public KeyedSwap SwapKeys(string first, string second)
{
    return new() { first = first, second = second };
}

// Document metadata.

public element Html html {
    Html children;
}

public element Html head {
    Html children;
}

public element Html body {
    Html children;
}

public element Html title {
    Html children;
}

public element Html base {
    Option<Url> href;
    Option<string> target;
}

public element Html link {
    Option<Url> href;
    Option<string> crossorigin;
    Option<string> rel;
    Option<string> as;
    Option<string> media;
    Option<string> hreflang;
    Option<string> type;
    Option<string> sizes;
    Option<string> imagesrcset;
    Option<string> imagesizes;
    Option<string> referrerpolicy;
    Option<string> integrity;
    Option<string> blocking;
    Option<string> color;
    Option<bool> disabled;
    Option<string> fetchpriority;
}

public element Html meta {
    Option<string> name;
    Option<string> property;
    Option<string> content;
    Option<string> charset;
    Option<string> http-equiv;
    Option<string> media;
}

public element Html style {
    Option<string> media;
    Option<string> blocking;
    Html children;
}

public element Html script {
    Option<Url> src;
    Option<string> type;
    Option<bool> nomodule;
    Option<bool> async;
    Option<bool> defer;
    Option<string> crossorigin;
    Option<string> integrity;
    Option<string> referrerpolicy;
    Option<string> blocking;
    Option<string> fetchpriority;
    Html children;
}

public element Html noscript {
    Html children;
}

public element Html template {
    Option<string> shadowrootmode;
    Option<bool> shadowrootdelegatesfocus;
    Option<string> shadowrootslotassignment;
    Option<bool> shadowrootclonable;
    Option<bool> shadowrootserializable;
    Html children;
}

// Sections and headings.

public element Html address { Html children; }
public element Html article { Html children; }
public element Html aside { Html children; }
public element Html footer { Html children; }
public element Html header { Html children; }
public element Html h1 { Html children; }
public element Html h2 { Html children; }
public element Html h3 { Html children; }
public element Html h4 { Html children; }
public element Html h5 { Html children; }
public element Html h6 { Html children; }
public element Html hgroup { Html children; }
public element Html main { Html children; }
public element Html nav { Html children; }
public element Html search { Html children; }
public element Html section { Html children; }

// Grouping content.

public element Html blockquote {
    Option<Url> cite;
    Html children;
}

public element Html div { Html children; }

public element Html dl { Html children; }
public element Html dt { Html children; }
public element Html dd { Html children; }

public element Html figure { Html children; }
public element Html figcaption { Html children; }

public element Html hr {}

public element Html li {
    Option<long> value;
    Html children;
}

public element Html menu { Html children; }

public element Html ol {
    Option<bool> reversed;
    Option<long> start;
    Option<string> type;
    Html children;
}

public element Html p { Html children; }
public element Html pre { Html children; }
public element Html ul { Html children; }

// Text-level semantics.

public element Html a {
    Option<Url> href;
    Option<string> target;
    Option<string> download;
    Option<string> ping;
    Option<string> rel;
    Option<string> hreflang;
    Option<string> type;
    Option<string> referrerpolicy;
    Html children;
}

public element Html abbr { Html children; }
public element Html b { Html children; }
public element Html bdi { Html children; }
public element Html bdo { Html children; }
public element Html br {}
public element Html cite { Html children; }
public element Html code { Html children; }

public element Html data {
    string value;
    Html children;
}

public element Html dfn { Html children; }
public element Html em { Html children; }
public element Html i { Html children; }
public element Html kbd { Html children; }
public element Html mark { Html children; }

public element Html q {
    Option<Url> cite;
    Html children;
}

public element Html ruby { Html children; }
public element Html rp { Html children; }
public element Html rt { Html children; }
public element Html s { Html children; }
public element Html samp { Html children; }
public element Html small { Html children; }
public element Html span { Html children; }
public element Html strong { Html children; }
public element Html sub { Html children; }
public element Html sup { Html children; }

public element Html time {
    Option<string> datetime;
    Html children;
}

public element Html u { Html children; }
public element Html var { Html children; }
public element Html wbr {}

public element Html del {
    Option<Url> cite;
    Option<string> datetime;
    Html children;
}

public element Html ins {
    Option<Url> cite;
    Option<string> datetime;
    Html children;
}

// Embedded content.

public element Html area {
    string alt;
    Option<string> coords;
    Option<string> shape;
    Option<Url> href;
    Option<string> target;
    Option<string> download;
    Option<string> ping;
    Option<string> rel;
    Option<string> referrerpolicy;
}

public element Html audio {
    Option<Url> src;
    Option<string> crossorigin;
    Option<string> preload;
    Option<bool> autoplay;
    Option<string> loading;
    Option<bool> loop;
    Option<bool> muted;
    Option<bool> controls;
    Html children;
}

public element Html canvas {
    Option<long> width;
    Option<long> height;
    Html children;
}

public element Html embed {
    Option<Url> src;
    Option<string> type;
    Option<long> width;
    Option<long> height;
}

public element Html iframe {
    Option<Url> src;
    Option<string> srcdoc;
    Option<string> name;
    Option<string> sandbox;
    Option<string> allow;
    Option<bool> allowfullscreen;
    Option<long> width;
    Option<long> height;
    Option<string> referrerpolicy;
    Option<string> loading;
}

public element Html img {
    Url src;
    string alt;
    Option<string> srcset;
    Option<string> sizes;
    Option<string> crossorigin;
    Option<string> usemap;
    Option<bool> ismap;
    Option<long> width;
    Option<long> height;
    Option<string> referrerpolicy;
    Option<string> decoding;
    Option<string> loading;
    Option<string> fetchpriority;
}

public element Html map {
    string name;
    Html children;
}

public element Html object {
    Option<Url> data;
    Option<string> type;
    Option<string> name;
    Option<string> form;
    Option<long> width;
    Option<long> height;
    Html children;
}

public element Html picture { Html children; }

public element Html source {
    Option<string> type;
    Option<string> media;
    Option<Url> src;
    Option<string> srcset;
    Option<string> sizes;
    Option<long> width;
    Option<long> height;
}

public element Html track {
    Option<bool> default;
    Option<string> kind;
    Option<string> label;
    Url src;
    Option<string> srclang;
}

public element Html video {
    Option<Url> src;
    Option<string> crossorigin;
    Option<Url> poster;
    Option<string> preload;
    Option<bool> autoplay;
    Option<bool> playsinline;
    Option<string> loading;
    Option<bool> loop;
    Option<bool> muted;
    Option<bool> controls;
    Option<long> width;
    Option<long> height;
    Html children;
}

// Tables.

public element Html caption { Html children; }

public element Html col {
    Option<long> span;
}

public element Html colgroup {
    Option<long> span;
    Html children;
}

public element Html table { Html children; }
public element Html tbody { Html children; }

public element Html td {
    Option<long> colspan;
    Option<long> rowspan;
    Option<string> headers;
    Html children;
}

public element Html tfoot { Html children; }

public element Html th {
    Option<long> colspan;
    Option<long> rowspan;
    Option<string> headers;
    Option<string> scope;
    Option<string> abbr;
    Html children;
}

public element Html thead { Html children; }
public element Html tr { Html children; }

// Forms and interactive content.

public element Html button {
    Option<string> command;
    Option<string> commandfor;
    Option<bool> disabled;
    Option<string> form;
    Option<Url> formaction;
    Option<string> formenctype;
    Option<string> formmethod;
    Option<bool> formnovalidate;
    Option<string> formtarget;
    Option<string> name;
    Option<string> popovertarget;
    Option<string> popovertargetaction;
    Option<string> type;
    Option<string> value;
    Html children;
}

public element Html datalist { Html children; }

public element Html details {
    Option<string> name;
    Option<bool> open;
    Html children;
}

public element Html summary { Html children; }

public element Html dialog {
    Option<bool> open;
    Html children;
}

public element Html fieldset {
    Option<bool> disabled;
    Option<string> form;
    Option<string> name;
    Html children;
}

public element Html form {
    Option<string> accept-charset;
    Option<Url> action;
    Option<string> autocomplete;
    Option<string> enctype;
    Option<string> method;
    Option<string> name;
    Option<bool> novalidate;
    Option<string> rel;
    Option<string> target;
    Html children;
}

public element Html input {
    Option<string> accept;
    Option<string> alt;
    Option<string> autocomplete;
    Option<bool> checked;
    Option<string> dirname;
    Option<bool> disabled;
    Option<string> form;
    Option<Url> formaction;
    Option<string> formenctype;
    Option<string> formmethod;
    Option<bool> formnovalidate;
    Option<string> formtarget;
    Option<long> height;
    Option<string> list;
    Option<string> max;
    Option<long> maxlength;
    Option<string> min;
    Option<long> minlength;
    Option<bool> multiple;
    Option<string> name;
    Option<string> pattern;
    Option<string> placeholder;
    Option<string> popovertarget;
    Option<string> popovertargetaction;
    Option<bool> readonly;
    Option<bool> required;
    Option<long> size;
    Option<Url> src;
    Option<string> step;
    Option<string> type;
    Option<string> value;
    Option<long> width;
}

public element Html label {
    Option<string> for;
    Html children;
}

public element Html legend { Html children; }

public element Html meter {
    double value;
    Option<double> min;
    Option<double> max;
    Option<double> low;
    Option<double> high;
    Option<double> optimum;
    Html children;
}

public element Html optgroup {
    Option<bool> disabled;
    string label;
    Html children;
}

public element Html option {
    Option<bool> disabled;
    Option<string> label;
    Option<bool> selected;
    Option<string> value;
    Html children;
}

public element Html output {
    Option<string> for;
    Option<string> form;
    Option<string> name;
    Html children;
}

public element Html progress {
    Option<double> value;
    Option<double> max;
    Html children;
}

public element Html select {
    Option<string> autocomplete;
    Option<bool> disabled;
    Option<string> form;
    Option<bool> multiple;
    Option<string> name;
    Option<bool> required;
    Option<long> size;
    Html children;
}

public element Html selectedcontent {}

public element Html textarea {
    Option<string> autocomplete;
    Option<long> cols;
    Option<string> dirname;
    Option<bool> disabled;
    Option<string> form;
    Option<long> maxlength;
    Option<long> minlength;
    Option<string> name;
    Option<string> placeholder;
    Option<bool> readonly;
    Option<bool> required;
    Option<long> rows;
    Option<string> wrap;
    Html children;
}

// Web components.

public element Html slot {
    Option<string> name;
    Html children;
}
