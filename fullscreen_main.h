/***
 Elie's OpenGL wrapper
 Creates a fullscreen vsync'd context with game-style access to keyboard & mouse.
 Version 1.1
***/
#pragma once

// This header file implements main().
// Here are the functions you must define in your own code:
void init();
void draw();
void done();


/** Notes:
 *
 * The init() function is called once at startup.  This is the place to set up your shaders, textures, display lists, etc.
 * The draw() function is called at every frame.   This is the place for your OpenGL drawing code, such as calling your lists etc.
 * The done() function is called once at shutdown. This is the place to delete your shaders, textures, display lists, etc.
 *
 * The program keeps running until the user presses ESC.    (This can be overridden; see below)
 *
 * To access keyboard and mouse within your draw() function, read the following globals:
 *  keymap[] : ascii keys - For example, to check if the 'W' key is held down: code:  if (keymap['W']) { ... }  Note that you must use uppercase letters here.  Also, if you want to respond to a keypress only once when it's pressed (instead of on every frame the key is held down): code:  if (keymap['W']==KEY_FRESHLY_PRESSED) { ... }
 *  special_keymap[] : non-ascii keys, as defined by GLUT constants.
 *  _mouse_dx: mouse pointer motion (as fraction of window size) per frame
 *  _mouse_dy: mouse pointer motion (as fraction of window size) per frame
 * Or,
 * if you like standard controls (W,A,S,D + arrow keys), you can use these macros:
 *  keyboard_dz() : forward / backward
 *  keyboard_dy() :    jump / duck
 *  keyboard_dx() :      strafe
 * 
 * Other globals:
 *  _screen_x, _screen_y      : The screen width & height, in pixels
 *  _screen_size              : Geometric average of width & height. Useful as a general "screen size" no matter the aspect ratio. Think of it as "if all the screen's pixels were rearranged in a square, it would be how many pixels across" = sqrt(_screen_x * _screen_y)
 *  _global_argc, _global_argv: Same as argc & argv from main().
 *
 * Compiler flags needed when you use this header file:
 *      -ffast-math -O -lGL -lglut -lm
 *
 * defines available:
 *  #define NO_ESCAPE           : Disable the default 'ESC to quit' functionality. And then you'll need the line '_exit_the_program = GL_TRUE;' somewhere in your draw() code. Otherwise your program will never quit, and you'll be stuck in fullscreen mode forever.
 *                                Proceed with caution. Beware of accidentally making a program that never exits!
 *
 *  #define VIEWPORT_SCALE 2.0  : This will effectively double your "viewport" size. In other words, the edges of your screen will be at coordinates -0.5 to 0.5, instead of -1.0 to 1.0.
 *                                You might need this feature is you're using a lot of GL_POINTS with point sprites, to prevent them from popping (appearing/disappearing when they are near the screen edges). This is a workaround for OpenGL's badly-designed standard for point-sprites.
 *                                Hopefully this won't actually render extra pixels that you never see. I benchmarked it on an old Radeon graphics card and there was no performance loss. No wasted computations as far as I know. But it should probably be tested on more hardware just to make sure.
 *                                P.S. You can also use other values for VIEWPORT_SCALE, such as 1.2 or 1.5. Just beware of scales too big - you don't want your viewport to exceed GL_MAX_VIEWPORT_DIMS.
 *
 *  #define USE_PRE_INIT        : You get to define an extra function 'pre_init()' which gets called BEFORE the fullscreen window/context gets created. This gives you a chance to quit before the program starts, by setting _exit_the_program = GL_TRUE;
 *                                One use-case might be: Your program requires command-line arguments, and if the args are missing, you want to only display a message in the terminal (no OpenGL).
 *                                                       argc & argv are available as globals: _global_argc, _global_argv
 *
 *  #define USE_MULTISAMPLING   : Basic anti-aliasing mode. Good for triangles/quads/polygons, but useless for GL_POINTS sprites, usually.
 *
 *  #define SHOW_ERROR_LOG      : Report OpenGL errors in the terminal.
 *
 *  #define SHOW_FRAME_RATE     : Report the number of frames-per-second in the terminal.
 *
 *
 * Author: Elie Goldman Smith
 * This file is too trivial to copyright (since it's mostly boilerplate code),
 * so it's in the public domain.
 *
 * Feel free to copy this file into any project.
**/
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <math.h>
#ifdef USE_GLEW
 #include <GL/glew.h>
#else
 #define GL_GLEXT_PROTOTYPES
 #include <GL/gl.h>
 #include <GL/glext.h>
#endif
#include <GL/glut.h>
#define RND()   (rand()*(2.0f/RAND_MAX)-1.0f) // random number from -1 to 1
#define RND01() (rand()*(1.0f/RAND_MAX))      // random number from  0 to 1


int _screen_x = 8;
int _screen_y = 8;
GLfloat _screen_size = 8;

#define KEY_FRESHLY_PRESSED 3

unsigned _key_mod = 0;
char keymap[256] = {0};
char special_keymap[256] = {0};

// the 'gcb' prefix just stands for "glut call-back" function
void gcb_key_down(unsigned char key, int x, int y) {
 keymap[toupper(key)] = keymap[tolower(key)] = KEY_FRESHLY_PRESSED; // The toupper() and tolower() are to avoid a situation where, for example, the user holds 'shift', and then presses 'W', then releases the 'shift' and then releases the 'W' (which generates a lowercase 'w' keyup event instead).  XXX: This implementation still has some holes in it - for example numbers. We should really do something more universal like keymap[shift(key)] = keymap[shiftless(key)] = KEY_FRESHLY_PRESSED, but then we'd have to define the shift() and shiftless() functions, and they could get very complex if we want to support all locales.
 #ifndef NO_ESCAPE
 if (key == 27) exit(0);
 #endif
 _key_mod = glutGetModifiers();
}
void gcb_key_up  (unsigned char key, int x, int y) {
 keymap[toupper(key)] = keymap[tolower(key)] = 0;
 _key_mod = glutGetModifiers();
}
void gcb_special_key_down(int key, int x, int y) {
 special_keymap[(unsigned char)key] = KEY_FRESHLY_PRESSED;
 _key_mod = glutGetModifiers();
}
void gcb_special_key_up  (int key, int x, int y) {
 special_keymap[(unsigned char)key] = 0;
 _key_mod = glutGetModifiers();
}

#define keyboard_dz()     (!!keymap['W']-!!keymap['S']+!!special_keymap[GLUT_KEY_UP     ]-!!special_keymap[GLUT_KEY_DOWN     ]                            ) // forward/backward
#define keyboard_dy()     (!!keymap['Q']-!!keymap['Z']+!!special_keymap[GLUT_KEY_PAGE_UP]-!!special_keymap[GLUT_KEY_PAGE_DOWN]+!!keymap['E']-!!keymap['C']) // jump/duck
#define keyboard_dx()     (!!keymap['D']-!!keymap['A']+!!special_keymap[GLUT_KEY_RIGHT  ]-!!special_keymap[GLUT_KEY_LEFT     ]                            ) // strafe

int _exit_the_program = GL_FALSE;

GLfloat _mouse_x = 0; // mouse pointer position is
GLfloat _mouse_y = 0; // normalized to _screen_size
GLfloat _mouse_dx = 0;
GLfloat _mouse_dy = 0;
char _mouse_button_map[8] = {0};

void gcb_mouse_motion_with_pointer(int x, int y) {
 _mouse_dx = -_mouse_x;
 _mouse_dy = -_mouse_y;
 _mouse_x = (x - _screen_x/2) * 2.0f/_screen_size;
 _mouse_y = (y - _screen_y/2) *-2.0f/_screen_size;
 _mouse_dx += _mouse_x;
 _mouse_dy += _mouse_y;
}

void gcb_mouse_motion_pointerless(int x, int y) {
 int center_x = _screen_x/2;
 int center_y = _screen_y/2;
 x -= center_x;
 y -= center_y;
 if (x != 0 || y != 0) {
  _mouse_dx = x *-2.0f/_screen_size;
  _mouse_dy = y * 2.0f/_screen_size;
  glutWarpPointer(center_x, center_y);
 }
 _mouse_x = _mouse_y = 0;
}

void gcb_mouse_click(int button, int state, int x, int y) {
 if (button >= 0 && button < 8) _mouse_button_map[button] = (state==GLUT_DOWN)*KEY_FRESHLY_PRESSED;
 _mouse_x = (x - _screen_x/2) * 2.0f/_screen_size;
 _mouse_y = (y - _screen_y/2) *-2.0f/_screen_size;
 _key_mod = glutGetModifiers();
}

void show_mouse() {
 glutMotionFunc       (gcb_mouse_motion_with_pointer);
 glutPassiveMotionFunc(gcb_mouse_motion_with_pointer);
 glutSetCursor(GLUT_CURSOR_INHERIT);
}
void hide_mouse() {
 glutMotionFunc       (gcb_mouse_motion_pointerless);
 glutPassiveMotionFunc(gcb_mouse_motion_pointerless);
 glutSetCursor(GLUT_CURSOR_NONE);
 _mouse_x = _mouse_y = 0; glutWarpPointer(_screen_x/2, _screen_y/2);
}




void gcb_draw_frame()
{
 // clear any junk from the double buffer
 glPushAttrib(GL_ENABLE_BIT); glDepthMask(GL_TRUE); glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
 glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
 glPopAttrib();

 // render
 draw();
 if (_exit_the_program) exit(0);

 // remove the KEY_FRESHLY_PRESSED
 for (int i=0; i<256; i++) {
  keymap[i]         &= 1;
  special_keymap[i] &= 1;
 }
 for (int i=0; i<8; i++) {
  _mouse_button_map[i] &= 1;
 }
 // smooth out the times when mouse dx & dy don't get updated
 _mouse_dx *= 0.875f;
 _mouse_dy *= 0.875f;
 
 #ifdef SHOW_FRAME_RATE
 static int frame=0;
 if (++frame >= 64) {
  static int prev=0;
  int t = glutGet(GLUT_ELAPSED_TIME);
  if (prev) {
   printf(" %d FPS ", (int)(frame*1000.0/(t-prev)));
   fflush(stdout);
  }
  prev=t; frame=0;
 }
 #endif

 glutSwapBuffers();
}


void gcb_reshape_window(int width, int height)
{
 #ifdef VIEWPORT_SCALE
 glViewport((GLint)( width*0.5*(1.0-VIEWPORT_SCALE)),
            (GLint)(height*0.5*(1.0-VIEWPORT_SCALE)),
            (GLint)( width*VIEWPORT_SCALE),
            (GLint)(height*VIEWPORT_SCALE));
 #else
 glViewport(0, 0, (GLint)width, (GLint)height); // default behavior
 #endif
 _screen_x = width;
 _screen_y = height;
 _screen_size = sqrtf((float)_screen_x*_screen_y);
 _mouse_x = _mouse_y = 0; glutWarpPointer(_screen_x/2, _screen_y/2);
}


#ifdef SHOW_ERROR_LOG
void gcb_errors(GLenum source, GLenum type, GLuint id, GLenum severity,
                GLsizei length, const GLchar* message, const void* userParam) {
 fprintf(stderr,"GL CALLBACK: %s type = 0x%x, severity = 0x%x, message = %s\n",
           (type == GL_DEBUG_TYPE_ERROR ? "** GL ERROR **" : ""),
            type, severity, message);
}
#endif
#ifdef USE_PRE_INIT
void pre_init();
#endif



int    _global_argc;
char **_global_argv;

int main(int argc, char **argv) {
 _global_argc = argc;
 _global_argv = argv;
 #ifdef USE_PRE_INIT
 pre_init();  if (_exit_the_program) return 1;
 #endif
 glutInitDisplayMode(GLUT_RGBA | GLUT_DOUBLE
 #ifndef NO_DEPTH_BUFFER
  | GLUT_DEPTH
 #endif
 #ifdef USE_MULTISAMPLING
  | GLUT_MULTISAMPLE
 #endif
 );
 glutInitWindowSize(256,144);
 glutInit(&argc, argv);
 glutCreateWindow("view");
 #ifdef USE_GLEW
 glewInit();
 #endif
 glutReshapeFunc(gcb_reshape_window);
 glutKeyboardFunc(gcb_key_down);
 glutKeyboardUpFunc(gcb_key_up);
 glutSpecialFunc(gcb_special_key_down);
 glutSpecialUpFunc(gcb_special_key_up);
 glutSetKeyRepeat(GLUT_KEY_REPEAT_OFF);
 glutMouseFunc(gcb_mouse_click);
 glutMotionFunc(gcb_mouse_motion_pointerless);
 glutPassiveMotionFunc(gcb_mouse_motion_pointerless);
 glutSetCursor(GLUT_CURSOR_NONE);
 glutDisplayFunc(gcb_draw_frame);
 glutIdleFunc(gcb_draw_frame);
 glutFullScreen();

 #ifdef SHOW_ERROR_LOG
 glEnable(GL_DEBUG_OUTPUT);
 glDebugMessageCallback(gcb_errors, 0);
 #endif

 init();
 atexit(done);
 glutMainLoop();
 return 0;
}

/*
 // other client code you might need:
 if (_key_mod & GLUT_ACTIVE_SHIFT) {}
 if (_key_mod & GLUT_ACTIVE_CTRL)  {}
 if (_key_mod & GLUT_ACTIVE_ALT)   {}
*/
