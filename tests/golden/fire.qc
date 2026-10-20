$modelname "fire.mdl"
$cd "."
$cdtexture "."
$scale 1.0

$bbox -29.00 -30.00 -137.00 30.00 30.00 142.00

$texrendermode "fire.bmp" additive

$body "studio" "fire_ref"

$sequence "idle" "fire_anim" loop fps 30
