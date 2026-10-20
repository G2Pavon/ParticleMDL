$modelname "smoke.mdl"
$cd "."
$cdtexture "."
$scale 1.0

$bbox -38.00 -34.00 -36.00 42.00 35.00 137.00

$texrendermode "smoke.bmp" additive

$body "studio" "smoke_ref"

$sequence "idle" "smoke_anim" loop fps 15
