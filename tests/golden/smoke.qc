$modelname "smoke.mdl"
$cd "."
$cdtexture "."
$scale 1.0
$origin 0 0 0 -90

$bbox -49.00 -51.00 -18.00 55.00 47.00 157.00

$texrendermode "smoke.bmp" additive

$body "studio" "smoke_ref"

$sequence "idle" "smoke_anim" loop fps 60
