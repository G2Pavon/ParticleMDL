$modelname "explosion.mdl"
$cd "."
$cdtexture "."
$scale 1.0

$bbox -129.00 -129.00 -131.00 115.00 107.00 128.00

$texrendermode "explosion.bmp" additive

$body "studio" "explosion_ref"

$sequence "idle" "explosion_anim" fps 30
