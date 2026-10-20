$modelname "vortex.mdl"
$cd "."
$cdtexture "."
$scale 1.0

$bbox -257.00 -260.00 -20.00 270.00 259.00 20.00

$texrendermode "vortex.bmp" additive

$body "studio" "vortex_ref"

$sequence "idle" "vortex_anim" loop fps 15
