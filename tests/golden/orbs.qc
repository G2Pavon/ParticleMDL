$modelname "orbs.mdl"
$cd "."
$cdtexture "."
$scale 1.0
$origin 0 0 0 -90

$bbox -39.00 -135.00 -94.00 160.00 133.00 157.00

$texrendermode "orbs.bmp" additive

$body "studio" "orbs_ref"

$sequence "idle" "orbs_anim" loop fps 15
