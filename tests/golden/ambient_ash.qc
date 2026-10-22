$modelname "ambient_ash.mdl"
$cd "."
$cdtexture "."
$scale 1.0

$bbox -101.00 -108.00 -95.00 131.00 132.00 168.00

$texrendermode "ambient_ash.bmp" additive

$body "studio" "ambient_ash_ref"

$sequence "idle" "ambient_ash_anim" loop fps 15
