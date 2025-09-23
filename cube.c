#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define WIDTH 640
#define HEIGHT 480

// Internal framebuffer in 16-bit RGB565
uint16_t framebuffer[WIDTH * HEIGHT];
// Converted 32-bit framebuffer for X11 display
uint32_t framebuffer32[WIDTH * HEIGHT];
// Depth buffer (camera-space z)
float zbuffer[WIDTH * HEIGHT];

typedef struct { float x, y, z, w; } vec4;
typedef struct { float m[4][4]; } mat4;

// Draw a pixel in RGB565
static inline void putPixelRGB565_indexed(int idx, uint32_t color) {
    uint8_t r = (color >> 16) & 0xFF;
    uint8_t g = (color >> 8) & 0xFF;
    uint8_t b = color & 0xFF;
    uint16_t r5 = (r >> 3) & 0x1F;
    uint16_t g6 = (g >> 2) & 0x3F;
    uint16_t b5 = (b >> 3) & 0x1F;
    framebuffer[idx] = (r5 << 11) | (g6 << 5) | b5;
}

void drawPixel(int x, int y, uint32_t color) {
    if (x < 0 || x >= WIDTH || y < 0 || y >= HEIGHT) return;
    int idx = y * WIDTH + x;
    putPixelRGB565_indexed(idx, color);
}

// Clear both color and depth buffers
void clearBuffers(uint32_t color) {
    uint8_t r = (color >> 16) & 0xFF;
    uint8_t g = (color >> 8) & 0xFF;
    uint8_t b = color & 0xFF;
    uint16_t r5 = (r >> 3) & 0x1F;
    uint16_t g6 = (g >> 2) & 0x3F;
    uint16_t b5 = (b >> 3) & 0x1F;
    uint16_t packed = (r5 << 11) | (g6 << 5) | b5;

    for (int i = 0; i < WIDTH * HEIGHT; i++) {
        framebuffer[i] = packed;
        zbuffer[i] = 1e9f;
    }
}

// Convert 16-bit RGB565 -> 32-bit RGB for X11
void convert_to_ximage() {
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

// matrix multiply
mat4 mul_mat4(mat4 a, mat4 b) {
    mat4 r;
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            r.m[i][j] = a.m[i][0] * b.m[0][j] +
                        a.m[i][1] * b.m[1][j] +
                        a.m[i][2] * b.m[2][j] +
                        a.m[i][3] * b.m[3][j];
        }
    }
    return r;
}

// Multiply vec4 * mat4 (row-vector style)
vec4 mul_vec4_mat4(vec4 v, mat4 m) {
    vec4 r;
    r.x = v.x*m.m[0][0] + v.y*m.m[1][0] + v.z*m.m[2][0] + v.w*m.m[3][0];
    r.y = v.x*m.m[0][1] + v.y*m.m[1][1] + v.z*m.m[2][1] + v.w*m.m[3][1];
    r.z = v.x*m.m[0][2] + v.y*m.m[1][2] + v.z*m.m[2][2] + v.w*m.m[3][2];
    r.w = v.x*m.m[0][3] + v.y*m.m[1][3] + v.z*m.m[2][3] + v.w*m.m[3][3];
    return r;
}

// Helper: edge function (signed area * 2)
static inline float edgeFunction(float ax, float ay, float bx, float by, float cx, float cy) {
    return (cx - ax) * (by - ay) - (cy - ay) * (bx - ax);
}

// Rasterize triangle with barycentric coordinates and z-buffer
void drawTriangleZ(vec4 v0, vec4 v1, vec4 v2, uint32_t color) {
    if (v0.z <= 0.0f || v1.z <= 0.0f || v2.z <= 0.0f) return;

    float scale = 200.0f;
    float sx0 = (float)WIDTH/2.0f + (v0.x / v0.z) * scale;
    float sy0 = (float)HEIGHT/2.0f - (v0.y / v0.z) * scale;
    float sz0 = v0.z;
    float sx1 = (float)WIDTH/2.0f + (v1.x / v1.z) * scale;
    float sy1 = (float)HEIGHT/2.0f - (v1.y / v1.z) * scale;
    float sz1 = v1.z;
    float sx2 = (float)WIDTH/2.0f + (v2.x / v2.z) * scale;
    float sy2 = (float)HEIGHT/2.0f - (v2.y / v2.z) * scale;
    float sz2 = v2.z;

    float minXf = fminf(fminf(sx0, sx1), sx2);
    float maxXf = fmaxf(fmaxf(sx0, sx1), sx2);
    float minYf = fminf(fminf(sy0, sy1), sy2);
    float maxYf = fmaxf(fmaxf(sy0, sy1), sy2);
    int minX = (int)floorf(minXf);
    int maxX = (int)ceilf (maxXf);
    int minY = (int)floorf(minYf);
    int maxY = (int)ceilf (maxYf);

    if (minX < 0) minX = 0;
    if (minY < 0) minY = 0;
    if (maxX >= WIDTH) maxX = WIDTH - 1;
    if (maxY >= HEIGHT) maxY = HEIGHT - 1;

    float area = edgeFunction(sx0, sy0, sx1, sy1, sx2, sy2);
    if (fabsf(area) < 1e-6f) return;

    for (int y = minY; y <= maxY; y++) {
        for (int x = minX; x <= maxX; x++) {
            float px = (float)x + 0.5f;
            float py = (float)y + 0.5f;
            float w0 = edgeFunction(sx1, sy1, sx2, sy2, px, py) / area;
            float w1 = edgeFunction(sx2, sy2, sx0, sy0, px, py) / area;
            float w2 = edgeFunction(sx0, sy0, sx1, sy1, px, py) / area;
            if (w0 >= -1e-6f && w1 >= -1e-6f && w2 >= -1e-6f) {
                float z = w0 * sz0 + w1 * sz1 + w2 * sz2;
                int idx = y * WIDTH + x;
                if (z < zbuffer[idx]) {
                    zbuffer[idx] = z;
                    putPixelRGB565_indexed(idx, color);
                }
            }
        }
    }
}

int main() {
    Display *dpy = XOpenDisplay(NULL);
    if (!dpy) return 1;
    int screen = DefaultScreen(dpy);
    Window win = XCreateSimpleWindow(dpy, RootWindow(dpy, screen), 0, 0, WIDTH, HEIGHT,
                                     1, BlackPixel(dpy, screen), WhitePixel(dpy, screen));
    XSelectInput(dpy, win, ExposureMask | KeyPressMask);
    XMapWindow(dpy, win);
    GC gc = DefaultGC(dpy, screen);

    XImage *ximage = XCreateImage(dpy, DefaultVisual(dpy, screen), 24, ZPixmap,
                                  0, malloc(WIDTH * HEIGHT * 4), WIDTH, HEIGHT, 32, 0);
    if (!ximage || !ximage->data) return 1;

    vec4 cube[8] = {
        {-1,-1,-1,1},{1,-1,-1,1},{1,1,-1,1},{-1,1,-1,1},
        {-1,-1,1,1},{1,-1,1,1},{1,1,1,1},{-1,1,1,1}
    };
    int tris[12][3] = {
        {0,1,2},{0,2,3},{4,5,6},{4,6,7},
        {0,1,5},{0,5,4},{2,3,7},{2,7,6},
        {1,2,6},{1,6,5},{0,3,7},{0,7,4}
    };
    int cubeTriangleColors[12] = {
        0xFF0000,0xFF0000,0x00FF00,0x00FF00,
        0x0000FF,0x0000FF,0xFFFF00,0xFFFF00,
        0x00FFFF,0x00FFFF,0xFF00FF,0xFF00FF
    };

    float angleX = 0.0f;
    float angleY = 0.0f;

    while (1) {
        XEvent e;
        while (XPending(dpy)) {
            XNextEvent(dpy, &e);
            if (e.type == KeyPress) goto cleanup;
        }

        clearBuffers(0x000000);

        mat4 rotX = { .m = {
            {1,0,0,0},
            {0, cosf(angleX), sinf(angleX), 0},
            {0,-sinf(angleX), cosf(angleX), 0},
            {0,0,0,1}
        }};
        mat4 rotY = { .m = {
            { cosf(angleY),0, sinf(angleY),0},
            { 0,1,0,0},
            {-sinf(angleY),0, cosf(angleY),0},
            { 0,0,0,1}
        }};
        mat4 rot = mul_mat4(rotY, rotX);

        angleY += 0.02f;
        angleX += 0.03f;

        for (int i=0; i<12; i++) {
            vec4 v0 = mul_vec4_mat4(cube[tris[i][0]], rot);
            vec4 v1 = mul_vec4_mat4(cube[tris[i][1]], rot);
            vec4 v2 = mul_vec4_mat4(cube[tris[i][2]], rot);
            v0.z += 3.0f;
            v1.z += 3.0f;
            v2.z += 3.0f;
            drawTriangleZ(v0, v1, v2, (uint32_t)cubeTriangleColors[i]);
        }

        convert_to_ximage();
        memcpy(ximage->data, framebuffer32, WIDTH * HEIGHT * 4);
        XPutImage(dpy, win, gc, ximage, 0, 0, 0, 0, WIDTH, HEIGHT);
        usleep(10000);
    }

cleanup:
    XDestroyImage(ximage);
    XCloseDisplay(dpy);
    return 0;
}
