$modelname "laser_burst.mdl"
$cd "."
$cdtexture "."
$scale 1.0
$origin 0 0 0 -90

$bbox -26.00 -150.00 -36.00 158.00 154.00 36.00

$texrendermode "laser_burst.bmp" additive

$body "studio" "laser_burst_ref"

$sequence "idle" "laser_burst_anim" loop fps 30
