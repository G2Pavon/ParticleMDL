$modelname "sun.mdl"
$cd "."
$cdtexture "."
$scale 1.0

$bbox -24.00 -24.00 -18.00 25.00 25.00 18.00

$texrendermode "sun.bmp" additive

$body "studio" "sun_ref"

$sequence "idle" "sun_anim" loop fps 15
