$modelname "ambient_ash.mdl"
$cd "."
$cdtexture "."
$scale 1.0
$origin 0 0 0 -90

$bbox -94.00 -107.00 -98.00 130.00 131.00 164.00

$texrendermode "ambient_ash.bmp" additive

$body "studio" "ambient_ash_ref"

$sequence "idle" "ambient_ash_anim" loop fps 15
