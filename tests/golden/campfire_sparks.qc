$modelname "campfire_sparks.mdl"
$cd "."
$cdtexture "."
$scale 1.0

$bbox -35.00 -31.00 -6.00 35.00 34.00 132.00

$texrendermode "campfire_sparks.bmp" additive

$body "studio" "campfire_sparks_ref"

$sequence "idle" "campfire_sparks_anim" loop fps 30
