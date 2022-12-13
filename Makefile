tangent : tangent.c fullscreen_main.h text-quads.h
	gcc tangent.c -o tangent -O3 -ffast-math -lGL -lglut -lm -lpthread

better :  tangent.c fullscreen_main.h text-quads.h
	gcc tangent.c -o tangent -O3 -ffast-math -lGL -lglut -lm -lpthread --define USE_MULTISAMPLING

clean :
	rm tangent