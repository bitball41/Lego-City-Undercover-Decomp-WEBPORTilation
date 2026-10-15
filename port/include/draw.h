#pragma once

#include "numath/types.h"

void DrawBegin(int width, int height);
void DrawSetCamera(const NUVEC& eye, const NUVEC& target, float fovY);
void DrawTriangle(const NUVEC& a, const NUVEC& b, const NUVEC& c, int r, int g, int blue);
void DrawBox(const NUVEC& mn, const NUVEC& mx, int r, int g, int b);
void DrawMesh(const float* xyz, int vertexCount, const unsigned* indices, int indexCount, const NUVEC& pos,
              float yaw, float scale, int r, int g, int b);
void DrawText(int x, int y, const char* text, int r, int g, int b);
void DrawRect(int x, int y, int w, int h, int r, int g, int b, int a);
void DrawFinish();
const unsigned char* DrawPixels();
int DrawWidth();
int DrawHeight();
