$modelname "fire.mdl"
$cd "."
$cdtexture "."
$scale 1.0
$origin 0 0 0 -90

$bbox -28.00 -29.00 -137.00 29.00 29.00 140.00

$texrendermode "fire.bmp" additive

$body "studio" "fire_ref"

$sequence "idle" "fire_anim" loop fps 30
