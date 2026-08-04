<?php

declare(strict_types=1);

function mix(int $value, int $iteration): int
{
    $next = $value + $iteration + 1;
    if ($next > 1_000_000_000) {
        return $next - 1_000_000_000;
    }
    return $next;
}

$value = 1;
$iteration = 0;
while ($iteration < 10_000_000) {
    $value = mix($value, $iteration);
    $iteration++;
}
echo $value, "\n";
