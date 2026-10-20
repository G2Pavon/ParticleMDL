$modelname "sparks.mdl"
$cd "."
$cdtexture "."
$scale 1.0

$bbox -136.00 -136.00 -101.00 116.00 119.00 91.00

$texrendermode "sparks.bmp" additive

$body "studio" "sparks_ref"

$sequence "idle" "sparks_anim" loop fps 30
