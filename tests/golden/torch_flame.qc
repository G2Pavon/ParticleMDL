$modelname "torch_flame.mdl"
$cd "."
$cdtexture "."
$scale 1.0
$origin 0 0 0 -90

$bbox -14.00 -13.00 -7.00 14.00 13.00 10.00

$texrendermode "torch_flame.bmp" additive

$body "studio" "torch_flame_ref"

$sequence "idle" "torch_flame_anim" loop fps 30
