<?php

declare(strict_types=1);

function escape_text(string $value): string
{
    return htmlspecialchars(
        $value,
        ENT_QUOTES | ENT_SUBSTITUTE | ENT_HTML5,
        'UTF-8'
    );
}

function render_card(int $id, string $title, bool $active): string
{
    ob_start();
    ?><article class="card" data-id="<?= $id ?>"><h2><?=
        escape_text($title)
    ?></h2><p>Customer #<?= $id ?> is <?=
        $active ? 'active' : 'paused'
    ?>.</p></article><?php
    $output = ob_get_clean();
    if (!is_string($output)) {
        throw new RuntimeException('failed to finish HTML buffer');
    }
    return $output;
}

echo render_card(0, 'A&B <Aster>', true), "\n";

$total = 0;
$active = true;
for ($index = 0; $index < 200_000; $index++) {
    $total += strlen(render_card(
        $index,
        'A&B <Aster>',
        $active
    ));
    $active = !$active;
}
echo $total, "\n";
