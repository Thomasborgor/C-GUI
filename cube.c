#include <X11/Xlib.h> //includes for every library
#include <X11/Xutil.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

#define WIDTH 640 //define the screen size
#define HEIGHT 480

uint16_t framebuffer[WIDTH * HEIGHT]; //16 bit framebuffer, early testing stuff
uint32_t framebuffer32[WIDTH * HEIGHT]; //what x11 wants

typedef struct { float x, y, z, w; } vec4; //3d point class kind of, w is a placeholder for fast 4x4 matrix multiplication
typedef struct { float m[4][4]; } mat4; //this is for rotation multiplictation and stuff

static inline void putPixelRGB565_indexed(int idx, uint32_t color) { //code to put a pixel in the 16 bit buffer
    uint8_t r = (color >> 16) & 0xFF;
    uint8_t g = (color >> 8) & 0xFF;
    uint8_t b =  color        & 0xFF;
    uint16_t r5 = (r >> 3) & 0x1F;
    uint16_t g6 = (g >> 2) & 0x3F;
    uint16_t b5 = (b >> 3) & 0x1F;
    framebuffer[idx] = (r5 << 11) | (g6 << 5) | b5;
}

void drawPixel(int x, int y, uint32_t color) {
    if (x < 0 || x >= WIDTH || y < 0 || y >= HEIGHT) return;
    putPixelRGB565_indexed(y * WIDTH + x, color); //idk some stupid code
}

void clearScreen(uint32_t color) { //clears the screen a certain color
    uint8_t r = (color >> 16) & 0xFF;
    uint8_t g = (color >> 8) & 0xFF;
    uint8_t b =  color        & 0xFF;
    uint16_t r5 = (r >> 3) & 0x1F;
    uint16_t g6 = (g >> 2) & 0x3F;
    uint16_t b5 = (b >> 3) & 0x1F;
    uint16_t packed = (r5 << 11) | (g6 << 5) | b5;
    for (int i = 0; i < WIDTH * HEIGHT; i++) framebuffer[i] = packed;
}

void convert_to_ximage() { //converts 16 bit framebuffer to 32 bit
    for (int i = 0; i < WIDTH * HEIGHT; i++) {
        uint16_t c = framebuffer[i];
        uint8_t r5 = (c >> 11) & 0x1F;
        uint8_t g6 = (c >> 5) & 0x3F;
        uint8_t b5 = c & 0x1F;
        uint8_t r = (r5 << 3) | (r5 >> 2);
        uint8_t g = (g6 << 2) | (g6 >> 4);
        uint8_t b = (b5 << 3) | (b5 >> 2);
        framebuffer32[i] = (r << 16) | (g << 8) | b;
    }
}

mat4 mul_mat4(mat4 a, mat4 b) { //fast 4x4 matrix multiplication
    mat4 r;
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            r.m[i][j] = a.m[i][0]*b.m[0][j] + a.m[i][1]*b.m[1][j] +
                        a.m[i][2]*b.m[2][j] + a.m[i][3]*b.m[3][j];
    return r;
}

vec4 mul_vec4_mat4(vec4 v, mat4 m) { //for multiplying the rotation matrix to a traingles point
    vec4 r;
    r.x = v.x*m.m[0][0] + v.y*m.m[1][0] + v.z*m.m[2][0] + v.w*m.m[3][0];
    r.y = v.x*m.m[0][1] + v.y*m.m[1][1] + v.z*m.m[2][1] + v.w*m.m[3][1];
    r.z = v.x*m.m[0][2] + v.y*m.m[1][2] + v.z*m.m[2][2] + v.w*m.m[3][2];
    r.w = v.x*m.m[0][3] + v.y*m.m[1][3] + v.z*m.m[2][3] + v.w*m.m[3][3];
    return r;
}

static void drawLine(int x0,int y0,int x1,int y1,uint32_t color){ //draws line using bresenham's line algorithm
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy, e2;
    for (;;) {
        drawPixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void drawTriangle(vec4 v0, vec4 v1, vec4 v2, uint32_t color) { //draws a triangle, does simple perspective projection
    if (v0.z <= 0 || v1.z <= 0 || v2.z <= 0) return;
    float s = 300.0f;
    int x0 = (int)(WIDTH/2 + (v0.x / v0.z) * s);
    int y0 = (int)(HEIGHT/2 - (v0.y / v0.z) * s);
    int x1 = (int)(WIDTH/2 + (v1.x / v1.z) * s);
    int y1 = (int)(HEIGHT/2 - (v1.y / v1.z) * s);
    int x2 = (int)(WIDTH/2 + (v2.x / v2.z) * s);
    int y2 = (int)(HEIGHT/2 - (v2.y / v2.z) * s);

    //uint32_t white = 0xFFFFFF;
    drawLine(x0, y0, x1, y1, color);
    drawLine(x1, y1, x2, y2, color);
    drawLine(x2, y2, x0, y0, color);
}

int main() {
    Display *dpy = XOpenDisplay(NULL);
    if (!dpy) return 1;
    int screen = DefaultScreen(dpy);
    Window win = XCreateSimpleWindow(dpy, RootWindow(dpy, screen),
                                     0,0,WIDTH,HEIGHT,1,
                                     BlackPixel(dpy,screen),
                                     WhitePixel(dpy,screen));
    XSelectInput(dpy, win, ExposureMask | KeyPressMask);
    XMapWindow(dpy, win);
    GC gc = DefaultGC(dpy, screen);

    XImage *ximage = XCreateImage(dpy, DefaultVisual(dpy, screen),
                                  24, ZPixmap, 0, malloc(WIDTH * HEIGHT * 4),
                                  WIDTH, HEIGHT, 32, 0);
    if (!ximage || !ximage->data) return 1;

    vec4 cube[8] = {
        {-1,-1,-1,1},{1,-1,-1,1},{1,1,-1,1},{-1,1,-1,1},
        {-1,-1, 1,1},{1,-1, 1,1},{1,1, 1,1},{-1,1, 1,1}
    };
    int tris[12][3] = {
        {0,1,2},{0,2,3},{4,5,6},{4,6,7},
        {0,1,5},{0,5,4},{2,3,7},{2,7,6},
        {1,2,6},{1,6,5},{0,3,7},{0,7,4}
    };
    bool spaced = false;
    float ax=0, ay=0;
    for (;;) {
        XEvent e;
        while (XPending(dpy)) { 
            XNextEvent(dpy,&e); 
            KeySym sym = XLookupKeysym(&e.xkey, 0);
           // printf("key: %d", sym);
            switch (sym) {
                case XK_space:
                    spaced = !spaced; 
                    break;
                case XK_Left:
                    ay -= 0.1f; 
                    break;
                case XK_Right:
                    ay += 0.1f;
                    break;
                case XK_Up:
                    ax -= 0.1f;
                    break;
                case XK_Down:
                    ax += 0.1f;
                    break;
            }      

        }

        clearScreen(0x000000);

        mat4 rot = { .m = {
            { cosf(ay),0, sinf(ay),0},
            { 0,       1, 0,       0},
            {-sinf(ay),0, cosf(ay),0},
            { 0,0,0,1}
        }};
        mat4 rotX = { .m = {
            {1,0,0,0},
            {0,cosf(ax),-sinf(ax),0},
            {0,sinf(ax), cosf(ax),0},
            {0,0,0,1}
        }};
        rot = mul_mat4(rotX, rot);
        //mat4 rot = mul_mat4(rotY, rotX);
        if (!spaced) {
            ax += 0.03f; ay += 0.02f;
        }
        uint32_t colors[12] = {
            0xFF0000,0x00FF00,0x0000FF,0xFFFF00,
            0xFF00FF,0x00FFFF,0xFFFFFF,0x888888,
            0xFF8800,0x88FF00,0x0088FF,0x8800FF
        };
        for (int i=0;i<12;i++){
            vec4 v0 = mul_vec4_mat4(cube[tris[i][0]], rot);
            vec4 v1 = mul_vec4_mat4(cube[tris[i][1]], rot);
            vec4 v2 = mul_vec4_mat4(cube[tris[i][2]], rot);
            v0.z += 4; v1.z += 4; v2.z += 4;
            drawTriangle(v0, v1, v2, colors[i]);
        }

        convert_to_ximage();
        memcpy(ximage->data, framebuffer32, WIDTH * HEIGHT * 4);
        XPutImage(dpy, win, gc, ximage, 0,0,0,0, WIDTH, HEIGHT);
        usleep(16000);
    }
done:
    XDestroyImage(ximage);
    XCloseDisplay(dpy);
    return 0;
}
