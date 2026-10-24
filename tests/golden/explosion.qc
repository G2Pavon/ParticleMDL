$modelname "explosion.mdl"
$cd "."
$cdtexture "."
$scale 1.0
$origin 0 0 0 -90

$bbox -113.00 -124.00 -180.00 129.00 129.00 84.00

$texrendermode "explosion.bmp" additive

$body "studio" "explosion_ref"

$sequence "idle" "explosion_anim" fps 30
