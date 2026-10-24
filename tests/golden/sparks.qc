$modelname "sparks.mdl"
$cd "."
$cdtexture "."
$scale 1.0
$origin 0 0 0 -90

$bbox -134.00 -134.00 -100.00 127.00 121.00 90.00

$texrendermode "sparks.bmp" additive

$body "studio" "sparks_ref"

$sequence "idle" "sparks_anim" loop fps 30
