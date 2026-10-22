$modelname "laser_burst.mdl"
$cd "."
$cdtexture "."
$scale 1.0

$bbox -27.00 -158.00 -86.00 164.00 162.00 36.00

$texrendermode "laser_burst.bmp" additive

$body "studio" "laser_burst_ref"

$sequence "idle" "laser_burst_anim" loop fps 30
