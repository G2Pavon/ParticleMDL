$modelname "sun.mdl"
$cd "."
$cdtexture "."
$scale 1.0
$origin 0 0 0 -90

$bbox -18.00 -18.00 -18.00 18.00 18.00 18.00

$texrendermode "sun.bmp" additive

$body "studio" "sun_ref"

$sequence "idle" "sun_anim" loop fps 15
