$modelname "vortex.mdl"
$cd "."
$cdtexture "."
$scale 1.0

$bbox -272.00 -260.00 -20.00 271.00 259.00 20.00

$texrendermode "vortex.bmp" additive

$body "studio" "vortex_ref"

$sequence "idle" "vortex_anim" loop fps 15
