$modelname "smoke.mdl"
$cd "."
$cdtexture "."
$scale 1.0

$bbox -45.00 -48.00 -40.00 51.00 49.00 147.00

$texrendermode "smoke.bmp" additive

$body "studio" "smoke_ref"

$sequence "idle" "smoke_anim" loop fps 15
