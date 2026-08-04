# Routing experiments

Four runnable variants express the same Nook-shaped page set without adding
Aster syntax:

- `imperative`: one explicit method/path dispatcher;
- `router`: the existing typed `std.http_app.Router`;
- `pages`: literal page files plus a prototype generated registry;
- `hybrid`: page files for literal URLs and `Router` for parameterized URLs.

Run each from the repository root:

```sh
./build/lang project run examples/routing_experiments/aster.toml imperative
./build/lang project run examples/routing_experiments/aster.toml router
./build/lang project run examples/routing_experiments/aster.toml pages
./build/lang project run examples/routing_experiments/aster.toml hybrid
```

The page registry is checked in deliberately. It registers discovered literal
pages into the ordinary typed router; dynamic routes use that same table. A
separate page dispatcher was rejected because its 404 result cannot cleanly
mean “fall through to dynamic routes.” This experiment does not yet justify
adding automatic page discovery to the compiler or CLI.

Current generated-C measurements for the shared Nook-shaped shell:

| Variant | Application lines | Generated C | Finding |
| --- | ---: | ---: | --- |
| imperative | 27 | 74,266 bytes | smallest and most direct |
| router | 26 | 79,942 bytes | concise registration; reusable dispatch |
| pages | 14 plus generated registry | 77,735 bytes | fastest literal-page authoring shape |
| hybrid | 26 plus generated registry | 81,331 bytes | literal and parameterized routes compose |

The generated sizes include Aster runtime support and are not standalone
router costs. More importantly, the variants show that file-based routing does
not need a second runtime abstraction: page discovery can generate ordinary
`router_get_mut` calls. H2O or another transport can therefore invoke the same
`router_dispatch` boundary used by the current HTTP experiment.
