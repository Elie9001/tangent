tangent: tangent.c fullscreen_main.h text-quads.h
	gcc tangent.c -o tangent -O -ffast-math -lGL -lglut -lm -lpthread