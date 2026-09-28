// A concentrated amber/red energy burst, built from the stock Force texture.
gfx/jof/destruction_energy
{
	cull disable
	{
		clampmap gfx/misc/lightningFlash
		blendFunc GL_SRC_ALPHA GL_ONE
		rgbGen vertex
		alphaGen vertex
	}
}

gfx/jof/force_destruction
{
	nomipmaps
	nopicmip
	{
		clampmap gfx/misc/lightningFlash
		blendFunc GL_ONE GL_ONE
		rgbGen const ( 1 0.22 0.04 )
	}
	{
		clampmap gfx/misc/lightningFlash
		blendFunc GL_ONE GL_ONE
		rgbGen const ( 1 0.8 0.45 )
		tcMod transform 2 0 0 2 -0.5 -0.5
	}
}
