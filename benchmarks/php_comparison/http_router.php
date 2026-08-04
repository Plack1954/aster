<?php

declare(strict_types=1);

header('Content-Type: text/html; charset=utf-8');
$path = parse_url($_SERVER['REQUEST_URI'], PHP_URL_PATH);
$escapedPath = htmlspecialchars(
    is_string($path) ? $path : '/',
    ENT_QUOTES | ENT_SUBSTITUTE | ENT_HTML5,
    'UTF-8'
);
echo '<main><h1>Aster versus PHP</h1><p>Path: ', $escapedPath, '</p></main>';
