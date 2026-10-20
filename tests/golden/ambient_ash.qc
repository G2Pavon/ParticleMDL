$modelname "ambient_ash.mdl"
$cd "."
$cdtexture "."
$scale 1.0

$bbox -199.00 -212.00 -187.00 259.00 260.00 332.00

$texrendermode "ambient_ash.bmp" additive

$body "studio" "ambient_ash_ref"

$sequence "idle" "ambient_ash_anim" loop fps 15
