$modelname "sparks.mdl"
$cd "."
$cdtexture "."
$scale 1.0
$origin 0 0 0 -90

$bbox -131.00 -130.00 -93.00 124.00 118.00 90.00

$texrendermode "sparks.bmp" additive

$body "studio" "sparks_ref"

$sequence "idle" "sparks_anim" loop fps 30
