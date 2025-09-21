arc:
	gcc trajectory.c -o trajectory -lX11 -lm
	./trajectory
cube:
	gcc cube.c -o cube -lm -lX11
	./cube