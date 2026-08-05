using Aster.Html;

int main() {
    Html stylesheet = <style>
        :root {
            --accent: color-mix(in oklab, aster 75%, black);
            --future-block: { preserved: exactly; };
        }

        .card,
        .panel:is(.active, [data-ready="yes"]) {
            display: grid;
            grid-template-columns: minmax(0, 1fr) auto;

            & > .title {
                color: var(--accent);
            }
        }

        @media (width < 48rem) {
            .card { grid-template-columns: 1fr; }
        }

        @future-layout experimental(value) {
            .card { future-property: unknown-function(1 / 2); }
        }

        @layer components;
        @supports selector(.card:has(> .title)) {
            .card { contain: inline-size; }
        }
        @container card (width > 30rem) {
            .title { font-size: clamp(1rem, 2cqi, 2rem); }
        }
        @keyframes reveal {
            from { opacity: 0; }
            50% { opacity: 0.5; }
            to { opacity: 1; }
        }
    </style>;
    Console.WriteLine(stylesheet.ToHtmlString());
    return 0;
}
