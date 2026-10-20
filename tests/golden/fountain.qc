$modelname "fountain.mdl"
$cd "."
$cdtexture "."
$scale 1.0

$bbox -66.00 -67.00 -207.00 68.00 68.00 64.00

$texrendermode "fountain.bmp" additive

$body "studio" "fountain_ref"

$sequence "idle" "fountain_anim" loop fps 30
