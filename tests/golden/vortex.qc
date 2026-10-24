$modelname "vortex.mdl"
$cd "."
$cdtexture "."
$scale 1.0
$origin 0 0 0 -90

$bbox -136.00 -139.00 -20.00 145.00 136.00 20.00

$texrendermode "vortex.bmp" additive

$body "studio" "vortex_ref"

$sequence "idle" "vortex_anim" loop fps 15
