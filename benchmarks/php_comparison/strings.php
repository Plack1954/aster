<?php

declare(strict_types=1);

function record(int $index, bool $active): string
{
    $activeText = $active ? 'true' : 'false';
    return "customer-{$index}:active={$activeText}:balance=" . ($index * 3);
}

echo record(0, true), "\n";

$total = 0;
$active = true;
for ($index = 0; $index < 300_000; $index++) {
    $total += strlen(record($index, $active));
    $active = !$active;
}
echo $total, "\n";
