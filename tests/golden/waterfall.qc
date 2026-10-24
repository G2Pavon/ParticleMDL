$modelname "waterfall.mdl"
$cd "."
$cdtexture "."
$scale 1.0
$origin 0 0 0 -90

$bbox -67.00 -2.00 -275.00 67.00 2.00 12.00

$texrendermode "waterfall.bmp" additive

$body "studio" "waterfall_ref"

$sequence "idle" "waterfall_anim" loop fps 30
