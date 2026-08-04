<?php

declare(strict_types=1);

$value = 1;
$iteration = 0;
while ($iteration < 20_000_000) {
    $value = $value + $iteration + 1;
    if ($value > 1_000_000_000) {
        $value = $value - 1_000_000_000;
    }
    $iteration++;
}
echo $value, "\n";
