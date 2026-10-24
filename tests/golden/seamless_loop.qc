$modelname "seamless_loop.mdl"
$cd "."
$cdtexture "."
$scale 1.0
$origin 0 0 0 -90

$bbox -53.00 -49.00 -5.00 52.00 53.00 194.00

$texrendermode "seamless_loop.bmp" additive

$body "studio" "seamless_loop_ref"

$sequence "idle" "seamless_loop_anim" loop fps 30
