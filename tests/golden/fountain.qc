$modelname "fountain.mdl"
$cd "."
$cdtexture "."
$scale 1.0
$origin 0 0 0 -90

$bbox -66.00 -67.00 -205.00 68.00 68.00 63.00

$texrendermode "fountain.bmp" additive

$body "studio" "fountain_ref"

$sequence "idle" "fountain_anim" loop fps 30
